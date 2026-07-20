//
// Created by Chronos | 2026 | Belgium
//

/**
 * @file smart_account.hpp
 * @brief Declares protocol-level smart-account primitives for delayed settlement and daily limits.
 */

#pragma once

#include "util/bytes.hpp"
#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace chrono_ledger {

/**
 * @brief Represents a pending transaction that can still be revoked before settlement.
 */
struct PendingSettlement {
    std::string tx_id;
    uint64_t queued_at_seconds = 0;
};

/**
 * @brief Lightweight smart-account state used for BOMMA-PROOF guardrails.
 */
struct SmartAccountState {
    uint64_t daily_limit = 0;
    uint64_t spent_today = 0;
    std::deque<PendingSettlement> pending_settlement;
};

} // namespace chrono_ledger

