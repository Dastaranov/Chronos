//
// Created by Chronos | 2026 | Belgium
//

/**
 * @file time_oracle.hpp
 * @brief Hardware abstraction for Layer 1 beacon timestamp sourcing.
 */

#pragma once

#include <cstdint>
#include <chrono>

namespace chrono_hardware {

/**
 * @class ITimeOracle
 * @brief Abstract interface for obtaining hardware-backed timestamps.
 */
class ITimeOracle {
public:
    virtual ~ITimeOracle() = default;

    /**
     * @brief Returns a hardware-derived timestamp in milliseconds since Unix epoch.
     * @return Timestamp value in milliseconds.
     */
    virtual uint64_t get_hardware_timestamp() = 0;
    // TODO: Implement direct UART/1PPS integration for Microchip SA.65 CSAC module
};

/**
 * @class SimulatedClock
 * @brief Development/CI fallback oracle using system clock time.
 */
class SimulatedClock : public ITimeOracle {
public:
    /**
     * @brief Returns system clock time in milliseconds.
     * @return Timestamp value in milliseconds.
     */
    uint64_t get_hardware_timestamp() override {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()
            ).count()
        );
    }
};

} // namespace chrono_hardware
