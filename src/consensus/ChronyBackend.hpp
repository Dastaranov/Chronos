#pragma once
#include "consensus/ITimeSyncBackend.hpp"
#include <string>
#include <vector>

namespace chrono_consensus {

class ChronyBackend : public ITimeSyncBackend {
public:
    ChronyBackend();
    ~ChronyBackend() override = default;

    std::optional<TimeSample> query(const std::string& server) override;
    std::string get_name() const override { return "chrony_cli"; }

private:
    // Helper to execute shell command and get output
    std::string exec_command(const char* cmd);
    
    // Helper to parse chronyc output line
    std::optional<TimeSample> parse_chronyc_line(const std::string& line, const std::string& target_server);
};

} // namespace chrono_consensus
