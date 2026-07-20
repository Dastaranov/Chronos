//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file state.cpp
 * @brief Implements the State class methods for managing ledger account balances.
 *
 * This file provides the implementation details for the `State` class, including
 * mechanisms for loading and saving account balances to persistent storage,
 * retrieving individual account balances, and applying transactions to
 * modify these balances in a thread-safe manner. It uses nlohmann/json for serialization.
 */
#include "ledger/state.hpp"
#include "address/address.hpp"
#include "util/bytes.hpp"
#include "util/codec.hpp"
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>

namespace chrono_ledger {

/// @brief The key used to store and retrieve the entire balances map in the key-value store.
const chrono_util::Bytes BALANCES_KEY = chrono_util::string_to_bytes("_BALANCES_");
constexpr uint64_t ENERGY_TOKEN_TTL_SECONDS = 900;
constexpr uint64_t PENDING_SETTLEMENT_SECONDS = 60;

/**
 * @brief Constructs a State object.
 * @param kv_store A reference to an `IKv` implementation for state persistence.
 */
State::State(chrono_storage::IKv& kv_store) : kv_store_(kv_store) {
    // Upon construction, load the balances from the key-value store.
    load_balances();
    pruning_thread_ = std::thread(&State::pruning_worker_loop, this);
}

State::~State() {
    stop_background_tasks_ = true;
    prune_cv_.notify_all();
    if (pruning_thread_.joinable()) {
        pruning_thread_.join();
    }
}

uint64_t State::current_unix_seconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

/**
 * @brief Loads account balances from the key-value store.
 *
 * Retrieves the serialized balance data from storage using `BALANCES_KEY`.
 * It's a thread-safe operation. If data exists, it's parsed from JSON.
 * If parsing fails or if no data exists (e.g., first run), it initializes a genesis
 * state with a default balance for a hardcoded address and saves it.
 */
void State::load_balances() {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    auto balances_data = kv_store_.get(BALANCES_KEY);
    total_circulating_supply_ = 0;
    if (balances_data) {
        try {
            nlohmann::json balances_json = nlohmann::json::parse(*balances_data);
            
            // Check if it's the new format with "balances" key
            if (balances_json.contains("balances")) {
                balances_ = balances_json.at("balances").get<std::unordered_map<std::string, uint64_t>>();
                
                if (balances_json.contains("validator_keys")) {
                    auto keys_map = balances_json.at("validator_keys").get<std::unordered_map<std::string, std::string>>();
                    for (const auto& [addr, hex_key] : keys_map) {
                        validator_keys_[addr] = chrono_util::hex_to_bytes(hex_key);
                    }
                }

                if (balances_json.contains("node_registry")) {
                    node_registry_.from_json(balances_json.at("node_registry"));
                }
                if (balances_json.contains("daily_limits")) {
                    daily_limits_ = balances_json.at("daily_limits").get<std::unordered_map<std::string, uint64_t>>();
                }
                if (balances_json.contains("spent_today")) {
                    spent_today_ = balances_json.at("spent_today").get<std::unordered_map<std::string, uint64_t>>();
                }
                if (balances_json.contains("spent_day_epoch")) {
                    spent_day_epoch_ = balances_json.at("spent_day_epoch").get<std::unordered_map<std::string, uint64_t>>();
                }
            } else {
                // Legacy format: root object is the map
                balances_ = balances_json.get<std::unordered_map<std::string, uint64_t>>();
            }
            
            // Calculate total supply
            for (const auto& pair : balances_) {
                total_circulating_supply_ += pair.second;
            }
        } catch (const std::exception& e) {
            // If parsing fails, it could be due to corrupted data.
            // As a fallback, we initialize a genesis state.
            balances_.clear();
            balances_["cqc13sj2mrcc4faq9arcw63x6t3j9u2uhn4t9echrt"] = 1000000;
            total_circulating_supply_ = 1000000;
            save_balances();
        }
    } else {
        // If no balances are found, this is the first run. Initialize the genesis state.
        balances_["cqc13sj2mrcc4faq9arcw63x6t3j9u2uhn4t9echrt"] = 1000000;
        total_circulating_supply_ = 1000000;
        save_balances();
    }
}

/**
 * @brief Saves the current balance map to the key-value store.
 *
 * Serializes the `balances_` map to a JSON string and writes it to persistent storage.
 * This function should be called after any modification to the balances map to ensure persistence.
 * Note: This operation is not protected by the mutex; it's the caller's responsibility to lock.
 */
void State::save_balances() {
    nlohmann::json j;
    j["balances"] = balances_;
    
    std::unordered_map<std::string, std::string> keys_hex;
    for (const auto& [addr, bytes] : validator_keys_) {
        keys_hex[addr] = chrono_util::bytes_to_hex(bytes);
    }
    j["validator_keys"] = keys_hex;
    j["node_registry"] = node_registry_.to_json();
    j["daily_limits"] = daily_limits_;
    j["spent_today"] = spent_today_;
    j["spent_day_epoch"] = spent_day_epoch_;

    kv_store_.put(BALANCES_KEY, chrono_util::string_to_bytes(j.dump()));
}

/**
 * @brief Retrieves the balance for a given address.
 *
 * A mutex is used to ensure that the balance map is not being modified while it is being read.
 */
uint64_t State::get_balance(const std::string& addr) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    auto it = balances_.find(addr);
    if (it != balances_.end()) {
        return it->second;
    }
    return 0;
}

/**
 * @brief Retrieves the nonce for a given address.
 *
 * The nonce is incremented each time a transaction from this address is applied.
 * A mutex is used to ensure thread-safe access.
 */
uint64_t State::get_nonce(const std::string& addr) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    auto it = nonces_.find(addr);
    if (it != nonces_.end()) {
        return it->second;
    }
    return 0;  // Default nonce is 0
}

/**
 * @brief Applies a transaction to the state, updating account balances.
 *
 * This function performs the core logic of a transaction: debiting the sender and crediting the recipient.
 * It is an atomic operation protected by a mutex. It performs several checks:
 * 1. Sender exists.
 * 2. Total amount (amount + fee) does not overflow.
 * 3. Sender has sufficient funds.
 * 4. Recipient balance does not overflow upon receiving the amount.
 * If any check fails, the operation is aborted and no balances are changed (or are reverted).
 * On success, the new balances are saved to the database.
 */
bool State::apply_transaction(const Transaction& tx) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    std::string sender_addr = tx.sender.to_string();

    auto it_sender = balances_.find(sender_addr);
    if (it_sender == balances_.end()) {
        return false; // Sender not found
    }

    uint64_t sender_balance = it_sender->second;
    const uint64_t now_seconds = current_unix_seconds();
    process_pending_settlements_locked(now_seconds);

    switch (tx.type) {
        case TransactionType::STANDARD:
            if (!queue_pending_settlement_locked(tx, now_seconds)) {
                return false;
            }
            break;
        case TransactionType::ENERGY_MINT:
            if (!apply_energy_mint_locked(tx, sender_addr, sender_balance, now_seconds)) {
                return false;
            }
            break;
        case TransactionType::ENERGY_FALLBACK:
            if (!apply_energy_fallback_locked(tx, sender_addr, sender_balance, now_seconds)) {
                return false;
            }
            break;
        case TransactionType::ENERGY_MATCH:
            if (!apply_standard_transfer_locked(tx, sender_addr, sender_balance)) {
                return false;
            }
            break;
        case TransactionType::KEY_ROTATION:
            if (tx.amount > 0 || sender_balance < tx.fee || tx.payload.empty()) {
                return false;
            }
            balances_[sender_addr] = sender_balance - tx.fee;
            validator_keys_[sender_addr] = tx.payload;
            nonces_[sender_addr]++;
            if (total_circulating_supply_ >= tx.fee) {
                total_circulating_supply_ -= tx.fee;
            } else {
                total_circulating_supply_ = 0;
            }
            break;
        case TransactionType::STAKE_REGISTRATION: {
            if (tx.amount > (UINT64_MAX - tx.fee) || sender_balance < tx.amount + tx.fee) {
                return false;
            }
            balances_[sender_addr] = sender_balance - (tx.amount + tx.fee);

            uint8_t time_tier = 5;
            std::string name;
            if (!tx.payload.empty()) {
                if (tx.payload[0] >= 1 && tx.payload[0] <= 5) {
                    time_tier = tx.payload[0];
                    if (tx.payload.size() > 1) {
                        name = std::string(tx.payload.begin() + 1, tx.payload.end());
                    }
                } else {
                    name = std::string(tx.payload.begin(), tx.payload.end());
                }
            }
            std::string public_key_hex = chrono_util::bytes_to_hex(tx.public_key);
            if (!node_registry_.register_node(sender_addr, public_key_hex, name, tx.amount, time_tier)) {
                balances_[sender_addr] = sender_balance;
                return false;
            }
            if (total_circulating_supply_ >= tx.amount + tx.fee) {
                total_circulating_supply_ -= (tx.amount + tx.fee);
            } else {
                total_circulating_supply_ = 0;
            }
            nonces_[sender_addr]++;
            break;
        }
        case TransactionType::UNSTAKE: {
            auto node_opt = node_registry_.get_node(sender_addr);
            if (!node_opt || sender_balance < tx.fee) {
                return false;
            }

            uint64_t unstake_amount = tx.amount;
            if (unstake_amount == 0 || unstake_amount > node_opt->stake_nanos) {
                unstake_amount = node_opt->stake_nanos;
            }
            node_registry_.update_stake(sender_addr, -static_cast<int64_t>(unstake_amount));
            balances_[sender_addr] = sender_balance - tx.fee + unstake_amount;
            total_circulating_supply_ = total_circulating_supply_ - tx.fee + unstake_amount;
            nonces_[sender_addr]++;
            break;
        }
        case TransactionType::VOTE: {
            if (sender_balance < tx.fee) {
                return false;
            }
            std::string candidate_id(tx.payload.begin(), tx.payload.end());
            if (!node_registry_.vote_for_node(sender_addr, candidate_id)) {
                return false;
            }
            auto active_validators = node_registry_.get_active_validators(0);
            node_registry_.check_approval(candidate_id, active_validators.size());
            balances_[sender_addr] = sender_balance - tx.fee;
            if (total_circulating_supply_ >= tx.fee) {
                total_circulating_supply_ -= tx.fee;
            } else {
                total_circulating_supply_ = 0;
            }
            nonces_[sender_addr]++;
            break;
        }
        case TransactionType::PROPOSAL_UPGRADE:
            if (sender_balance < tx.fee) {
                return false;
            }
            balances_[sender_addr] = sender_balance - tx.fee;
            if (total_circulating_supply_ >= tx.fee) {
                total_circulating_supply_ -= tx.fee;
            } else {
                total_circulating_supply_ = 0;
            }
            nonces_[sender_addr]++;
            break;
        default:
            return false;
    }

    prune_expired_energy_tokens_locked(now_seconds);
    save_balances();
    return true;
}

bool State::apply_committed_standard_transfer_locked(const Transaction& tx,
                                                     const std::string& sender_addr,
                                                     uint64_t sender_balance,
                                                     bool increment_nonce) {
    if (tx.amount > (UINT64_MAX - tx.fee)) {
        return false;
    }
    const uint64_t total_amount = tx.amount + tx.fee;
    if (sender_balance < total_amount) {
        return false;
    }

    const uint64_t now_seconds = current_unix_seconds();
    if (!enforce_daily_limit_locked(sender_addr, tx.amount, now_seconds)) {
        return false;
    }

    std::string recipient_addr = tx.recipient.to_string();
    auto it_recipient = balances_.find(recipient_addr);
    uint64_t current_recipient_balance = (it_recipient != balances_.end()) ? it_recipient->second : 0;
    if (current_recipient_balance > (UINT64_MAX - tx.amount)) {
        return false;
    }

    balances_[sender_addr] = sender_balance - total_amount;
    balances_[recipient_addr] = current_recipient_balance + tx.amount;

    if (total_circulating_supply_ >= tx.fee) {
        total_circulating_supply_ -= tx.fee;
    } else {
        total_circulating_supply_ = 0;
    }

    if (increment_nonce) {
        nonces_[sender_addr]++;
    }
    return true;
}

bool State::apply_standard_transfer_locked(const Transaction& tx,
                                           const std::string& sender_addr,
                                           uint64_t sender_balance) {
    return apply_committed_standard_transfer_locked(tx, sender_addr, sender_balance, true);
}

bool State::queue_pending_settlement_locked(const Transaction& tx, uint64_t now_seconds) {
    const std::string tx_id = chrono_util::bytes_to_hex(tx.get_hash());
    auto duplicate = std::find_if(
        pending_settlement_.begin(), pending_settlement_.end(),
        [&tx_id](const PendingSettlementEntry& entry) { return entry.tx_id == tx_id; });
    if (duplicate != pending_settlement_.end()) {
        return false;
    }

    pending_settlement_.push_back(PendingSettlementEntry{tx, now_seconds, tx_id});
    return true;
}

bool State::enforce_daily_limit_locked(const std::string& sender_addr, uint64_t amount, uint64_t now_seconds) {
    const auto limit_it = daily_limits_.find(sender_addr);
    if (limit_it == daily_limits_.end() || limit_it->second == 0) {
        return true;
    }

    const uint64_t day_index = now_seconds / 86400;
    if (spent_day_epoch_[sender_addr] != day_index) {
        spent_day_epoch_[sender_addr] = day_index;
        spent_today_[sender_addr] = 0;
    }

    uint64_t current_spent = spent_today_[sender_addr];
    if (current_spent > (UINT64_MAX - amount)) {
        return false;
    }
    if (current_spent + amount > limit_it->second) {
        return false;
    }

    spent_today_[sender_addr] = current_spent + amount;
    return true;
}

bool State::apply_energy_mint_locked(const Transaction& tx,
                                     const std::string& sender_addr,
                                     uint64_t sender_balance,
                                     uint64_t now_seconds) {
    if (sender_balance < tx.fee) {
        return false;
    }

    uint64_t minted_amount = tx.amount;
    if (minted_amount == 0 && tx.payload.size() >= sizeof(uint64_t)) {
        size_t offset = 0;
        minted_amount = chrono_util::read_fixed_uint64_le(tx.payload, offset);
    }
    if (minted_amount == 0) {
        return false;
    }

    const std::string token_id = chrono_util::bytes_to_hex(tx.get_hash());
    const std::string owner = tx.recipient.to_string().empty() ? sender_addr : tx.recipient.to_string();
    energy_tokens_[token_id] = EnergyTokenMeta{owner, minted_amount, now_seconds};
    expiration_index_[now_seconds].push_back(token_id);

    balances_[sender_addr] = sender_balance - tx.fee;
    if (total_circulating_supply_ >= tx.fee) {
        total_circulating_supply_ -= tx.fee;
    } else {
        total_circulating_supply_ = 0;
    }
    nonces_[sender_addr]++;
    return true;
}

bool State::apply_energy_fallback_locked(const Transaction& tx,
                                         const std::string& sender_addr,
                                         uint64_t sender_balance,
                                         uint64_t now_seconds) {
    if (sender_balance < tx.fee) {
        return false;
    }

    uint64_t moved_amount = 0;
    std::vector<TokenID> consumed_ids;
    if (!tx.payload.empty()) {
        std::string token_id(tx.payload.begin(), tx.payload.end());
        auto token_it = energy_tokens_.find(token_id);
        if (token_it != energy_tokens_.end() &&
            token_it->second.owner == sender_addr &&
            now_seconds > token_it->second.created_at + ENERGY_TOKEN_TTL_SECONDS) {
            moved_amount += token_it->second.amount;
            consumed_ids.push_back(token_id);
        }
    } else {
        for (const auto& [token_id, token] : energy_tokens_) {
            if (token.owner == sender_addr &&
                now_seconds > token.created_at + ENERGY_TOKEN_TTL_SECONDS) {
                moved_amount += token.amount;
                consumed_ids.push_back(token_id);
            }
        }
    }

    for (const auto& token_id : consumed_ids) {
        energy_tokens_.erase(token_id);
    }

    uint64_t battery_balance = balances_[battery_park_address_];
    if (moved_amount > 0 && battery_balance <= (UINT64_MAX - moved_amount)) {
        balances_[battery_park_address_] = battery_balance + moved_amount;
    }

    balances_[sender_addr] = sender_balance - tx.fee;
    if (total_circulating_supply_ >= tx.fee) {
        total_circulating_supply_ -= tx.fee;
    } else {
        total_circulating_supply_ = 0;
    }
    nonces_[sender_addr]++;
    return true;
}

void State::process_pending_settlements_locked(uint64_t network_time_seconds) {
    while (!pending_settlement_.empty()) {
        const PendingSettlementEntry& entry = pending_settlement_.front();
        if (network_time_seconds < entry.enqueue_time + PENDING_SETTLEMENT_SECONDS) {
            break;
        }

        Transaction tx = entry.tx;
        pending_settlement_.pop_front();
        const std::string sender_addr = tx.sender.to_string();
        auto sender_it = balances_.find(sender_addr);
        if (sender_it == balances_.end()) {
            continue;
        }

        apply_committed_standard_transfer_locked(tx, sender_addr, sender_it->second, true);
    }
}

void State::prune_expired_energy_tokens_locked(uint64_t network_time_seconds) {
    if (network_time_seconds <= ENERGY_TOKEN_TTL_SECONDS) {
        return;
    }
    const uint64_t expiry_cutoff = network_time_seconds - ENERGY_TOKEN_TTL_SECONDS;
    uint64_t pruned_amount = 0;

    auto it = expiration_index_.begin();
    while (it != expiration_index_.end() && it->first <= expiry_cutoff) {
        for (const auto& token_id : it->second) {
            auto token_it = energy_tokens_.find(token_id);
            if (token_it != energy_tokens_.end()) {
                pruned_amount += token_it->second.amount;
                energy_tokens_.erase(token_it);
            }
        }
        it = expiration_index_.erase(it);
    }

    if (pruned_amount == 0) {
        return;
    }

    uint64_t battery_balance = balances_[battery_park_address_];
    if (battery_balance <= (UINT64_MAX - pruned_amount)) {
        balances_[battery_park_address_] = battery_balance + pruned_amount;
    }
}

void State::process_pending_settlements(uint64_t network_time_seconds) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    process_pending_settlements_locked(network_time_seconds);
    save_balances();
}

void State::prune_expired_energy_tokens(uint64_t network_time_seconds) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    prune_expired_energy_tokens_locked(network_time_seconds);
    save_balances();
}

bool State::revoke_transaction(const Bytes& tx_hash) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    const std::string tx_id = chrono_util::bytes_to_hex(tx_hash);
    auto it = std::find_if(
        pending_settlement_.begin(), pending_settlement_.end(),
        [&tx_id](const PendingSettlementEntry& entry) { return entry.tx_id == tx_id; });
    if (it == pending_settlement_.end()) {
        return false;
    }
    pending_settlement_.erase(it);
    save_balances();
    return true;
}

void State::set_daily_limit(const std::string& addr, uint64_t limit) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    daily_limits_[addr] = limit;
    save_balances();
}

void State::pruning_worker_loop() {
    std::unique_lock<std::mutex> lock(background_mutex_);
    while (!stop_background_tasks_) {
        prune_cv_.wait_for(lock, std::chrono::seconds(5));
        if (stop_background_tasks_) {
            break;
        }
        const uint64_t now_seconds = current_unix_seconds();
        std::lock_guard<std::mutex> state_lock(balances_mutex_);
        process_pending_settlements_locked(now_seconds);
        prune_expired_energy_tokens_locked(now_seconds);
        save_balances();
    }
}

/**
 * @brief Directly credits an account with a specified amount.
 *
 * This is a privileged operation used for minting new coins, such as for block rewards.
 * It is thread-safe and persists the new state. It includes an overflow check.
 */
void State::credit(const std::string& addr, uint64_t amount) {
    // Validate address format before crediting
    if (!chrono_address::Address::is_valid(addr)) {
        // Log error but don't throw - credit operations are often internal
        return;
    }
    
    std::lock_guard<std::mutex> lock(balances_mutex_);
    auto it = balances_.find(addr);
    uint64_t current_balance = (it != balances_.end()) ? it->second : 0;

    // Check for overflow before adding amount
    if (current_balance > (UINT64_MAX - amount)) {
        // Overflow would occur, do not credit
        return;
    }
    balances_[addr] = current_balance + amount;
    total_circulating_supply_ += amount;
    save_balances();
}

void State::slash_validator(const std::string& node_id, uint64_t penalty, const std::string& reason) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    // NodeRegistry has its own mutex, but we call it here.
    // Since we are in State, we should coordinate persistence.
    node_registry_.slash_node(node_id, penalty, reason);
    
    // Persist the change
    save_balances();
}

/**
 * @brief Retrieves the active public key for a validator.
 */
Bytes State::get_validator_key(const std::string& addr) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    auto it = validator_keys_.find(addr);
    if (it != validator_keys_.end()) {
        return it->second;
    }
    return {}; // Return empty if no rotation (implies default key)
}

/**
 * @brief Sets the balance for a given address (used for genesis allocation).
 *
 * This is a privileged operation typically used during genesis initialization.
 * Validates address format and checks balance doesn't exceed max_balance.
 */
void State::set_balance(const std::string& addr, uint64_t balance, uint64_t max_balance) {
    // Validate address format
    if (!chrono_address::Address::is_valid(addr)) {
        throw std::invalid_argument("Invalid address format: " + addr);
    }
    
    // Validate balance doesn't exceed maximum
    if (balance > max_balance) {
        throw std::invalid_argument("Balance " + std::to_string(balance) + 
                                   " exceeds max allowed " + std::to_string(max_balance) +
                                   " for address: " + addr);
    }
    
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    // Update supply
    auto it = balances_.find(addr);
    uint64_t old_balance = (it != balances_.end()) ? it->second : 0;
    if (balance > old_balance) {
        total_circulating_supply_ += (balance - old_balance);
    } else {
        total_circulating_supply_ -= (old_balance - balance);
    }

    balances_[addr] = balance;
    // Note: nonce remains 0 for genesis allocations
    save_balances();
}

/**
 * @brief Validates an address string format.
 */
bool State::is_valid_address(const std::string& addr) {
    return chrono_address::Address::is_valid(addr);
}

/**
 * @brief Serializes the entire state to a binary format using canonical little-endian encoding.
 *
 * Format (canonical, uses chrono_util::codec helpers):
 * - Magic: \"CSST\" (4 bytes)
 * - Version: 1 (uint32_t LE)
 * - Account count (uint32_t LE)
 * - For each account: addr_len (uint32_t LE) + addr_bytes + balance (uint64_t LE) + nonce (uint64_t LE)
 *
 * All multi-byte integers use little-endian byte order for deterministic cross-platform serialization.
 */
chrono_util::Bytes State::serialize_to_bytes() const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    chrono_util::Bytes result;
    
    // Magic bytes: "CSST"
    const char* magic = "CSST";
    result.insert(result.end(), magic, magic + 4);
    
    // Version: 2 (uint32_t LE)
    chrono_util::write_fixed_uint32_le(2, result);
    
    // Account count (uint32_t LE)
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(balances_.size()), result);
    
    // Serialize each account
    for (const auto& pair : balances_) {
        const std::string& addr = pair.first;
        uint64_t balance = pair.second;
        uint64_t nonce = 0;
        auto nonce_it = nonces_.find(addr);
        if (nonce_it != nonces_.end()) {
            nonce = nonce_it->second;
        }
        
        // Address string length (uint32_t LE) + address bytes
        chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(addr.length()), result);
        result.insert(result.end(), addr.begin(), addr.end());
        
        // Balance (uint64_t LE)
        chrono_util::write_fixed_uint64_le(balance, result);
        
        // Nonce (uint64_t LE)
        chrono_util::write_fixed_uint64_le(nonce, result);
    }

    // Serialize Validator Keys
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(validator_keys_.size()), result);
    for (const auto& pair : validator_keys_) {
        chrono_util::write_string_with_length(pair.first, result);
        chrono_util::write_bytes_with_length(pair.second, result);
    }

    // Serialize NodeRegistry
    chrono_util::Bytes registry_bytes = node_registry_.serialize_to_bytes();
    result.insert(result.end(), registry_bytes.begin(), registry_bytes.end());
    
    return result;
}

/**
 * @brief Deserializes state from binary format.
 *
 * Validates magic bytes and version before parsing accounts.
 */
bool State::deserialize_from_bytes(const chrono_util::Bytes& data) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    
    size_t offset = 0;
    
    // Validate minimum size (magic + version + count)
    if (data.size() < 4 + sizeof(uint32_t) + sizeof(uint32_t)) {
        return false;
    }
    
    // Check magic bytes
    if (data[0] != 'C' || data[1] != 'S' || data[2] != 'S' || data[3] != 'T') {
        return false;
    }
    offset += 4;
    
    // Check version (uint32_t LE)
    uint32_t version = chrono_util::read_fixed_uint32_le(data, offset);
    if (version != 1 && version != 2) {
        return false;  // Unsupported version
    }
    
    // Read account count (uint32_t LE)
    uint32_t account_count = chrono_util::read_fixed_uint32_le(data, offset);
    
    // Clear existing state
    balances_.clear();
    nonces_.clear();
    validator_keys_.clear();
    energy_tokens_.clear();
    expiration_index_.clear();
    pending_settlement_.clear();
    // node_registry_ cleared inside deserialize_from_bytes if called
    total_circulating_supply_ = 0;
    
    // Read each account
    for (uint32_t i = 0; i < account_count; ++i) {
        // Read address length (uint32_t LE)
        if (offset + sizeof(uint32_t) > data.size()) {
            return false;
        }
        uint32_t addr_len = chrono_util::read_fixed_uint32_le(data, offset);
        
        // Check we have enough data for address
        if (offset + addr_len > data.size()) {
            return false;
        }
        
        std::string addr(data.begin() + offset, data.begin() + offset + addr_len);
        offset += addr_len;
        
        // Check we have balance + nonce
        if (offset + sizeof(uint64_t) + sizeof(uint64_t) > data.size()) {
            return false;
        }
        
        // Read balance (uint64_t LE)
        uint64_t balance = chrono_util::read_fixed_uint64_le(data, offset);
        
        // Read nonce (uint64_t LE)
        uint64_t nonce = chrono_util::read_fixed_uint64_le(data, offset);
        
        balances_[addr] = balance;
        nonces_[addr] = nonce;
        total_circulating_supply_ += balance;
    }

    if (version >= 2) {
        // Read Validator Keys
        if (offset + sizeof(uint32_t) <= data.size()) {
            uint32_t key_count = chrono_util::read_fixed_uint32_le(data, offset);
            for (uint32_t i = 0; i < key_count; ++i) {
                std::string addr = chrono_util::read_string_with_length(data, offset);
                chrono_util::Bytes key = chrono_util::read_bytes_with_length(data, offset);
                validator_keys_[addr] = key;
            }
        }

        // Read NodeRegistry
        if (offset < data.size()) {
            if (!node_registry_.deserialize_from_bytes(data, offset)) {
                return false;
            }
        }
    }
    
    return true;
}

} // namespace chrono_ledger

bool chrono_ledger::State::validate_total_supply(uint64_t max_supply) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    return total_circulating_supply_ <= max_supply;
}

void chrono_ledger::State::update_circulating_supply(int64_t delta) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    if (delta > 0) {
        total_circulating_supply_ += static_cast<uint64_t>(delta);
    } else {
        uint64_t burn = static_cast<uint64_t>(-delta);
        total_circulating_supply_ = (total_circulating_supply_ >= burn) ? total_circulating_supply_ - burn : 0;
    }
}
