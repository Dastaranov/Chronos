//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file state.hpp
 * @brief Defines the State class, which manages the ledger's account balances.
 *
 * This file declares the `State` class, responsible for maintaining the blockchain's
 * current state, primarily account balances. It provides thread-safe mechanisms
 * for retrieving balances, applying transactions, and crediting accounts.
 * The state is persisted through an injected key-value store.
 */

#pragma once

#include "ledger/transaction.hpp"
#include "ledger/node_registry.hpp" // NEW: NodeRegistry
#include "storage/IKv.hpp" // Include IKv for persistence
#include <unordered_map>
#include <string>
#include <cstdint>
#include <mutex> // For thread-safe access to balances_

namespace chrono_ledger {

/**
 * @class State
 * @brief Manages the financial state (account balances) of the Chronos ledger.
 *
 * The `State` class acts as the central repository for all account balances on the blockchain.
 * It interfaces with a key-value store for persistence and ensures thread-safe access
 * to balance data. Key functionalities include loading/saving balances, retrieving
 * an account's balance, applying transactions that modify balances, and crediting accounts.
 */
class State {
public:
    /**
     * @brief Constructs a State object.
     *
     * Initializes the State manager with a reference to a key-value store for persistence.
     * It automatically loads the existing balances from the store upon construction.
     *
     * @param kv_store A reference to an `IKv` implementation for state persistence.
     */
    explicit State(chrono_storage::IKv& kv_store);

    /**
     * @brief Retrieves the current balance for a given address.
     *
     * This method provides thread-safe access to the balance of a specified account.
     * If the address does not exist in the state, it returns 0.
     *
     * @param addr The bech32m encoded string representation of the address.
     * @return The balance of the account as a `uint64_t`. Returns 0 if the address is not found.
     */
    uint64_t get_balance(const std::string& addr) const;

    /**
     * @brief Retrieves the current nonce for a given address.
     *
     * The nonce is incremented each time a transaction from this address is applied to the state.
     * It prevents replay attacks and ensures transactions from the same sender are processed in order.
     * If the address does not exist in the state, it returns 0.
     *
     * @param addr The bech32m encoded string representation of the address.
     * @return The nonce of the account as a `uint64_t`. Returns 0 if the address is not found.
     */
    uint64_t get_nonce(const std::string& addr) const;

    /**
     * @brief Applies a transaction to the current state, updating account balances.
     *
     * This method processes a `Transaction` by deducting the amount and fee from the sender's
     * balance and crediting the amount to the recipient's balance. It includes checks for
     * sufficient funds and potential overflows. The operation is thread-safe.
     *
     * @param tx The `Transaction` object to apply.
     * @return `true` if the transaction was successfully applied, `false` otherwise (e.g., insufficient funds, overflow).
     */
    bool apply_transaction(const Transaction& tx);

    /**
     * @brief Credits a specified amount to an address.
     *
     * This method adds a given amount to the balance of an account. It is used for operations
     * like coinbase rewards or initial coin distribution. The operation is thread-safe.
     *
     * @param addr The bech32m encoded string representation of the address to credit.
     * @param amount The `uint64_t` amount to add to the account's balance.
     */
    void credit(const std::string& addr, uint64_t amount);

    /**
     * @brief Slashes a validator for misbehavior.
     * 
     * Reduces the validator's stake in the registry.
     * Since the stake was already removed from circulating supply (locked),
     * slashing it effectively burns it.
     * 
     * @param node_id The validator's ID (address).
     * @param penalty The amount to slash in nanos.
     * @param reason The reason for slashing.
     */
    void slash_validator(const std::string& node_id, uint64_t penalty, const std::string& reason);

    /**
     * @brief Retrieves the active public key for a validator.
     * 
     * If the validator has rotated their key, this returns the latest key.
     * If no rotation has occurred, it returns an empty Bytes vector (implying the address itself is the key source).
     * 
     * @param addr The validator's address.
     * @return The active public key bytes, or empty if default.
     */
    Bytes get_validator_key(const std::string& addr) const;

    /**
     * @brief Serializes the entire state (balances and nonces) to a binary format.
     *
     * The binary format consists of:
     * - Magic bytes: "CSST" (Chronos State Snapshot Terrain) (4 bytes)
     * - Version: 1 (uint32_t LE)
     * - Account count: number of accounts (uint32_t LE)
     * - For each account:
     *   - Address string length (uint32_t LE) + address bytes
     *   - Balance (uint64_t LE)
     *   - Nonce (uint64_t LE)
     *
     * @return A Bytes object containing the serialized state.
     */
    chrono_util::Bytes serialize_to_bytes() const;

    /**
     * @brief Deserializes state from a binary format.
     *
     * Reconstructs the balances_ and nonces_ maps from binary data.
     * Validates magic bytes and version before parsing.
     *
     * @param data The serialized state bytes.
     * @return true if deserialization succeeded, false if format was invalid.
     */
    bool deserialize_from_bytes(const chrono_util::Bytes& data);

    /**
     * @brief Gets the total circulating supply.
     * @return The total supply in nanos.
     */
    uint64_t get_total_supply() const { return total_circulating_supply_; }

    /**
     * @brief Validates if the current supply is within the maximum limit.
     * @param max_supply The maximum allowed supply.
     * @return true if valid, false otherwise.
     */
    bool validate_total_supply(uint64_t max_supply) const;

    /**
     * @brief Updates the circulating supply.
     * @param delta The amount to add (positive) or subtract (negative).
     */
    void update_circulating_supply(int64_t delta);

    /**
     * @brief Sets the balance for a given address (used for genesis allocation).
     *
     * Validates the address format and checks for balance overflow against max supply.
     * This is a privileged operation typically used only during genesis initialization.
     *
     * @param addr The bech32m encoded address string.
     * @param balance The balance to set.
     * @param max_balance Maximum allowed balance per account (for overflow protection).
     * @throws std::invalid_argument if address is invalid or balance exceeds max.
     */
    void set_balance(const std::string& addr, uint64_t balance, uint64_t max_balance = UINT64_MAX);

    /**
     * @brief Validates an address string format.
     *
     * Checks if the address is a valid bech32m encoded string.
     *
     * @param addr The address string to validate.
     * @return true if valid, false otherwise.
     */
    static bool is_valid_address(const std::string& addr);

    /**
     * @brief Provides access to the node registry.
     * @return A const reference to the NodeRegistry.
     */
    const NodeRegistry& get_node_registry() const { return node_registry_; }

    /**
     * @brief Provides mutable access to the node registry.
     * @return A reference to the NodeRegistry.
     */
    NodeRegistry& get_node_registry_mutable() { return node_registry_; }

private:
    /**
     * @brief Loads account balances from the underlying key-value store.
     *
     * This internal method is called during initialization to populate the `balances_` map
     * from persistent storage. It handles deserialization of JSON data.
     */
    void load_balances();

    /**
     * @brief Saves current account balances to the underlying key-value store.
     *
     * This internal method serializes the `balances_` map into a JSON string and stores it
     * persistently using the injected `IKv` store.
     */
    void save_balances();

    ///< @var A map storing account balances, keyed by address string and valued by `uint64_t` amount.
    std::unordered_map<std::string, uint64_t> balances_;
    ///< @var A map storing account nonces, keyed by address string and valued by `uint64_t` nonce count.
    std::unordered_map<std::string, uint64_t> nonces_;
    ///< @var A map storing active public keys for validators, keyed by address string.
    std::unordered_map<std::string, Bytes> validator_keys_;
    ///< @var A reference to the key-value store used for persisting state data.
    chrono_storage::IKv& kv_store_; // Reverted to reference
    ///< @var Mutex to ensure thread-safe access to the `balances_` and `nonces_` maps.
    mutable std::mutex balances_mutex_;

    ///< @var Total circulating supply of tokens in nanos.
    uint64_t total_circulating_supply_ = 0;

    ///< @var Registry for node identity and staking.
    NodeRegistry node_registry_;
};

} // namespace chrono_ledger
