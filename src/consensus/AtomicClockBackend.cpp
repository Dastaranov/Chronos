#include "AtomicClockBackend.hpp"
#include "util/log.hpp"
#include <chrono>
#include <thread>

namespace chrono_consensus {

AtomicClockBackend::AtomicClockBackend(const std::string& device_path) 
    : device_path_(device_path) {
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "AtomicClockBackend initialized on device: {}", device_path_);
}

std::optional<TimeSample> AtomicClockBackend::query(const std::string& /*server*/) {
    // In a real implementation, this would read from the serial port (e.g. /dev/ttyS0)
    // and parse NMEA sentences like $GPRMC or $GPZDA to get precise time.
    // For simulation, we return system time with high precision metadata.
    
    // Simulate hardware read delay
    std::this_thread::sleep_for(std::chrono::microseconds(500));

    TimeSample sample;
    sample.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // Atomic clocks have very low offset and jitter relative to UTC (if synced)
    // Here we assume the system clock is being disciplined by this atomic clock
    sample.offset = std::chrono::milliseconds(0); 
    sample.rtt_ms = 0.01; // Negligible latency for local hardware
    sample.source = "Atomic Clock (" + device_path_ + ")";
    sample.authenticated = true; // Hardware is trusted locally
    sample.tier = 2; // Tier 2 for Atomic Clock
    
    return sample;
}

} // namespace chrono_consensus
