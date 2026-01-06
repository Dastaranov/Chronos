#include "consensus/ChronyBackend.hpp"
#include "util/log.hpp"
#include <array>
#include <memory>
#include <stdexcept>
#include <sstream>
#include <iostream>
#include <cstring>
#include <vector>
#include <cmath>

namespace chrono_consensus {

ChronyBackend::ChronyBackend() {
    // Check if chronyc is available
    if (system("which chronyc > /dev/null 2>&1") != 0) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "chronyc not found in PATH");
    }
}

std::optional<TimeSample> ChronyBackend::query(const std::string& server) {
    // We use 'chronyc -c sources' to get list and find the server
    // Redirect stderr to stdout to capture connection errors
    std::string output = exec_command("chronyc -c sources 2>&1");
    if (output.empty()) return std::nullopt;

    // Check for common Chrony error messages
    if (output.find("506 Cannot talk to daemon") != std::string::npos ||
        output.find("Connection refused") != std::string::npos) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Chrony daemon is not reachable. Is it running?");
        return std::nullopt;
    }

    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        auto sample = parse_chronyc_line(line, server);
        if (sample) return sample;
    }
    
    return std::nullopt;
}

std::string ChronyBackend::exec_command(const char* cmd) {
    std::array<char, 128> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "popen() failed!");
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

std::optional<TimeSample> ChronyBackend::parse_chronyc_line(const std::string& line, const std::string& target_server) {
    // Format: M,S,Name/IP,Stratum,Poll,Reach,LastRx,LastSample
    // Example: ^,*,time.cloudflare.com,3,6,377,12,+123ns[+456ns]
    
    std::stringstream ss(line);
    std::string segment;
    std::vector<std::string> parts;
    
    while(std::getline(ss, segment, ',')) {
        parts.push_back(segment);
    }
    
    if (parts.size() < 8) return std::nullopt;
    
    std::string source = parts[2];
    if (source != target_server) return std::nullopt;
    
    TimeSample sample;
    sample.source = source;
    sample.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
        
    // Parse LastSample (offset)
    // Format is like "+123ns[+456ns]" or "-1.2ms[-1.2ms]"
    std::string offset_str = parts[7];
    size_t bracket_pos = offset_str.find('[');
    if (bracket_pos != std::string::npos) {
        offset_str = offset_str.substr(0, bracket_pos);
    }
    
    // Simple parsing of unit (ns, us, ms, s)
    double offset_val = 0.0;
    try {
        size_t unit_pos = 0;
        offset_val = std::stod(offset_str, &unit_pos);
        std::string unit = offset_str.substr(unit_pos);
        
        if (unit == "ns") offset_val /= 1000000.0;
        else if (unit == "us") offset_val /= 1000.0;
        else if (unit == "s") offset_val *= 1000.0;
        // ms is default
        
    } catch (...) {
        return std::nullopt;
    }
    
    sample.offset = std::chrono::milliseconds(static_cast<long>(offset_val));
    sample.rtt_ms = 0.0; // Chrony sources output doesn't give RTT directly
    
    // Determine Tier
    // For now, we assume NTS if the server name contains "nts" or if configured.
    // Ideally we check 'chronyc authdata'
    if (source.find("nts") != std::string::npos) {
        sample.tier = 2; // NTS
        sample.authenticated = true;
    } else {
        sample.tier = 3; // NTP
        sample.authenticated = false;
    }
    
    return sample;
}

} // namespace chrono_consensus
