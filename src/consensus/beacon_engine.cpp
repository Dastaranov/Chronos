//
// Created by Chronos | 2026 | Belgium
//

/**
 * @file beacon_engine.cpp
 * @brief Implements the Layer 1 ChronosBeat production engine.
 */

#include "consensus/beacon_engine.hpp"
#include "crypto/crypto_provider.hpp"
#include "util/codec.hpp"

namespace chrono_consensus {

BeaconEngine::BeaconEngine(chrono_crypto::ISigner* signer,
                           std::unique_ptr<chrono_hardware::ITimeOracle> time_oracle,
                           uint64_t beacon_interval_ms)
    : signer_(signer),
      time_oracle_(std::move(time_oracle)),
      beacon_interval_ms_(beacon_interval_ms),
      last_beat_time_(std::chrono::steady_clock::now()) {}

std::optional<ChronosBeat> BeaconEngine::maybe_produce(const std::chrono::steady_clock::time_point& now) {
    if (!signer_ || !time_oracle_) {
        return std::nullopt;
    }

    if (now - last_beat_time_ < std::chrono::milliseconds(beacon_interval_ms_)) {
        return std::nullopt;
    }
    last_beat_time_ = now;

    ChronosBeat beat;
    beat.timestamp_ms = time_oracle_->get_hardware_timestamp();
    beat.producer_id = chrono_util::bytes_to_hex(signer_->get_public_key());
    const chrono_util::Bytes hash = compute_beat_hash(beat.timestamp_ms, beat.producer_id);
    beat.signature = signer_->sign_message(hash);
    return beat;
}

chrono_util::Bytes BeaconEngine::compute_beat_hash(uint64_t timestamp_ms, const std::string& producer_id) {
    chrono_util::Bytes payload = chrono_util::string_to_bytes("CHRONOS_BEAT_V1");
    chrono_util::write_fixed_uint64_le(timestamp_ms, payload);
    payload.insert(payload.end(), producer_id.begin(), producer_id.end());
    return chrono_crypto::get_crypto_provider()->hash(payload);
}

} // namespace chrono_consensus
