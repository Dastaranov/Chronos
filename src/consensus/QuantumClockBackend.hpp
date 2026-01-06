#pragma once
#include "ITimeSyncBackend.hpp"
#include <string>

namespace chrono_consensus {

class QuantumClockBackend : public ITimeSyncBackend {
public:
    explicit QuantumClockBackend(const std::string& device_path);
    ~QuantumClockBackend() override = default;

    std::optional<TimeSample> query(const std::string& server) override;
    std::string get_name() const override { return "QuantumClockBackend"; }

private:
    std::string device_path_;
};

} // namespace chrono_consensus
