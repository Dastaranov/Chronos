#pragma once
#include <optional>
#include <string>
#include <chrono>

namespace chrono_consensus {

struct TimeSample {
    std::chrono::milliseconds offset;  // Offset from local time
    double rtt_ms;                      // Round-trip time
    std::string source;                 // Server address
    bool authenticated;                 // True if NTS-secured
    uint64_t timestamp_ms;              // When sampled
    uint8_t tier;                       // Security tier (1-4)
    std::vector<uint8_t> signature;     // Optional signature
};

class ITimeSyncBackend {
public:
    virtual ~ITimeSyncBackend() = default;
    
    /**
     * @brief Query a specific time server
     * @param server Address of the server (IP or hostname)
     * @return Optional TimeSample if query successful
     */
    virtual std::optional<TimeSample> query(const std::string& server) = 0;
    
    /**
     * @brief Get backend name/type
     */
    virtual std::string get_name() const = 0;
};

} // namespace chrono_consensus
