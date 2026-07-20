//
// Created by Chronos | 2026 | Belgium
//

/**
 * @file beacon_engine.hpp
 * @brief Layer 1 ChronosBeat production engine for beacon nodes.
 */

#pragma once

#include "util/bytes.hpp"
#include "crypto/signer.hpp"
#include "hardware/time_oracle.hpp"
#include <memory>
#include <optional>
#include <chrono>
#include <string>

namespace chrono_consensus {

/**
 * @struct ChronosBeat
 * @brief Represents a signed Layer 1 time beacon.
 */
struct ChronosBeat {
    uint64_t timestamp_ms = 0;
    std::string producer_id;
    chrono_util::Bytes signature;
};

/**
 * @class BeaconEngine
 * @brief Produces signed ChronosBeat messages at a fixed interval.
 */
class BeaconEngine {
public:
    /**
     * @brief Constructs the beacon engine.
     * @param signer Signer used for beat signatures.
     * @param time_oracle Time source abstraction.
     * @param beacon_interval_ms Beat production interval in milliseconds.
     */
    BeaconEngine(chrono_crypto::ISigner* signer,
                 std::unique_ptr<chrono_hardware::ITimeOracle> time_oracle,
                 uint64_t beacon_interval_ms);

    /**
     * @brief Produces a new beat if the interval elapsed.
     * @param now Current steady clock time for interval checks.
     * @return Newly produced beat if due, otherwise nullopt.
     */
    std::optional<ChronosBeat> maybe_produce(const std::chrono::steady_clock::time_point& now);

    /**
     * @brief Computes canonical hash for a beat payload.
     * @param timestamp_ms Beat timestamp.
     * @param producer_id Producer identifier.
     * @return Hash bytes used for signing/verification.
     */
    static chrono_util::Bytes compute_beat_hash(uint64_t timestamp_ms, const std::string& producer_id);

private:
    chrono_crypto::ISigner* signer_;
    std::unique_ptr<chrono_hardware::ITimeOracle> time_oracle_;
    uint64_t beacon_interval_ms_;
    std::chrono::steady_clock::time_point last_beat_time_;
};

} // namespace chrono_consensus
