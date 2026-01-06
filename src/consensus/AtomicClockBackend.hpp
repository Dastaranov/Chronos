#pragma once
#include "ITimeSyncBackend.hpp"
#include <string>

namespace chrono_consensus {

class AtomicClockBackend : public ITimeSyncBackend {
public:
    explicit AtomicClockBackend(const std::string& device_path);
    ~AtomicClockBackend() override = default;

    std::optional<TimeSample> query(const std::string& server) override;
    std::string get_name() const override { return "AtomicClockBackend"; }

private:
    std::string device_path_;
};

} // namespace chrono_consensus
