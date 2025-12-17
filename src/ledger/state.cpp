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

namespace chrono_ledger {

/// @brief The key used to store and retrieve the entire balances map in the key-value store.
const chrono_util::Bytes BALANCES_KEY = chrono_util::string_to_bytes("_BALANCES_");

/**
 * @brief Constructs a State object.
 * @param kv_store A reference to an `IKv` implementation for state persistence.
 */
State::State(chrono_storage::IKv& kv_store) : kv_store_(kv_store) {
    // Upon construction, load the balances from the key-value store.
    load_balances();
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
    if (balances_data) {
        try {
            nlohmann::json balances_json = nlohmann::json::parse(*balances_data);
            balances_ = balances_json.get<std::unordered_map<std::string, uint64_t>>();
        } catch (const nlohmann::json::parse_error& e) {
            // If parsing fails, it could be due to corrupted data.
            // As a fallback, we initialize a genesis state.
            balances_.clear();
            balances_["cqc1qru6j2c93t8k6l3x5x6y6z6j6"] = 1000000;
            save_balances();
        }
    } else {
        // If no balances are found, this is the first run. Initialize the genesis state.
        balances_["cqc1qru6j2c93t8k6l3x5x6y6z6j6"] = 1000000;
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
    nlohmann::json balances_json = balances_;
    kv_store_.put(BALANCES_KEY, chrono_util::string_to_bytes(balances_json.dump()));
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
    std::string recipient_addr = tx.recipient.to_string();

    auto it_sender = balances_.find(sender_addr);
    if (it_sender == balances_.end()) {
        return false; // Sender not found
    }

    uint64_t sender_balance = it_sender->second;
    uint64_t total_amount = tx.amount + tx.fee;

    // Check for arithmetic overflow before calculating total amount
    if (tx.amount > (UINT64_MAX - tx.fee)) {
        return false; // Overflow
    }

    // Check for sufficient funds
    if (sender_balance < total_amount) {
        return false; // Insufficient funds
    }

    // Debit the sender
    balances_[sender_addr] = sender_balance - total_amount;
    
    // Credit the recipient
    auto it_recipient = balances_.find(recipient_addr);
    uint64_t current_recipient_balance = (it_recipient != balances_.end()) ? it_recipient->second : 0;

    // Check for overflow before crediting recipient
    if (current_recipient_balance > (UINT64_MAX - tx.amount)) {
        balances_[sender_addr] = sender_balance; // Revert sender's balance change on failure
        return false; // Overflow
    }
    balances_[recipient_addr] = current_recipient_balance + tx.amount;

    // Increment sender's nonce
    nonces_[sender_addr]++;

    // Persist the new state
    save_balances();
    return true;
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
    save_balances();
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
    
    // Version: 1 (uint32_t LE)
    chrono_util::write_fixed_uint32_le(1, result);
    
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
    if (version != 1) {
        return false;  // Unsupported version
    }
    
    // Read account count (uint32_t LE)
    uint32_t account_count = chrono_util::read_fixed_uint32_le(data, offset);
    
    // Clear existing state
    balances_.clear();
    nonces_.clear();
    
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
    }
    
    return true;
}

} // namespace chrono_ledger
