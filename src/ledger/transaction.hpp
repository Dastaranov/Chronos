//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file transaction.hpp
 * @brief This file defines the Transaction class, representing a transfer of value in the Chronos ledger.
 *
 * The Transaction class encapsulates all the necessary information for a value transfer between
 * two addresses on the Chronos blockchain. This includes the sender, recipient, amount, fee,
 * a cryptographic signature for authenticity, and a nonce to prevent replay attacks.
 * It provides methods for transaction creation, hashing for signing, serialization,
 * deserialization, JSON conversion, and validation.
 *
 * Key functionalities include:
 * - `Transaction()`: Default constructor.
 * - `Transaction(const chrono_address::Address& sender, ..., uint64_t nonce)`: Constructor for creating a new transaction.
 * - `get_hash_for_signing()`: Computes a hash of the transaction's core data for signing purposes.
 * - `serialize()`: Converts the transaction into a byte vector.
 * - `deserialize(const Bytes& data)`: Reconstructs a Transaction object from a byte vector.
 * - `to_json()`: Converts the transaction's data into a JSON object.
 * - `from_json(const nlohmann::json& j)`: Populates the transaction's data from a JSON object.
 * - `is_valid()`: Performs validation checks on the transaction's integrity.
 */

#pragma once

#include "util/bytes.hpp"
#include "address/address.hpp"
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp> // For JSON serialization/deserialization

namespace chrono_ledger {

/**
 * @enum TransactionType
 * @brief Defines the type of transaction.
 */
enum class TransactionType : uint8_t {
    STANDARD = 0,           ///< Standard value transfer
    ENERGY_MINT = 1,        ///< Creates a time-bound energy token from external energy data
    ENERGY_MATCH = 2,       ///< Matches energy token flows between participants
    ENERGY_FALLBACK = 3,    ///< Moves expired energy tokens to battery-park settlement
    TRANSFER = STANDARD,    ///< Backward-compatible alias for standard transfer
    KEY_ROTATION = 10,      ///< Validator key rotation
    STAKE_REGISTRATION = 11,///< Register as validator with stake
    UNSTAKE = 12,           ///< Withdraw stake
    VOTE = 13,              ///< Vote for a node approval
    PROPOSAL_UPGRADE = 14   ///< Propose a network upgrade
};

/**
 * @enum SecurityTier
 * @brief Defines the security tier used for transaction routing.
 */
enum class SecurityTier : uint8_t {
    STANDARD_RETAIL = 0,      ///< Standard Layer 2 retail transaction.
    CRITICAL_SETTLEMENT = 1   ///< Critical Layer 1 settlement transaction.
};

/**
 * @class Transaction
 * @brief Represents a single transaction in the Chronos blockchain.
 *
 * A transaction records the transfer of a certain `amount` from a `sender` address
 * to a `recipient` address, along with an associated `fee`. It includes a `signature`
 * from the sender to prove authenticity and a `nonce` to prevent replay attacks.
 * It also supports different transaction types and arbitrary payloads.
 */
class Transaction {
public:
    TransactionType type = TransactionType::STANDARD; ///< @var type The type of the transaction.
    chrono_address::Address sender; ///< @var sender The address of the transaction initiator.
    chrono_address::Address recipient; ///< @var recipient The address of the transaction receiver.
    uint64_t amount; ///< @var amount The value being transferred in the transaction.
    uint64_t fee; ///< @var fee The fee paid for processing this transaction.
    Bytes public_key; ///< @var public_key The sender's public key (required for verification).
    Bytes payload; ///< @var payload Arbitrary data associated with the transaction (e.g., new key).
    Bytes signature; ///< @var signature The cryptographic signature of the sender, proving authorization.
    uint64_t nonce; ///< @var nonce A unique number used to prevent replay attacks and order transactions from the same sender.
    SecurityTier tier = SecurityTier::STANDARD_RETAIL; ///< @var tier Security tier used for asymmetric dual-layer routing.

    /**
     * @brief Default constructor for the Transaction class.
     *
     * Initializes a Transaction object with default values. This constructor is useful for creating
     * an empty transaction that can be populated later.
     */
    Transaction() = default;

    /**
     * @brief Constructor for creating a new transaction.
     *
     * Initializes a new transaction with the specified sender, recipient, amount, fee, and nonce.
     * The signature is typically added after the transaction data has been hashed.
     *
     * @param sender The address of the sender.
     * @param recipient The address of the recipient.
     * @param amount The amount of value to transfer.
     * @param fee The transaction fee.
     * @param nonce The transaction nonce.
     * @param type The transaction type (default: STANDARD).
     * @param payload Arbitrary payload data (default: empty).
     * @param tier Security tier (default: STANDARD_RETAIL).
     */
    Transaction(const chrono_address::Address& sender,
                const chrono_address::Address& recipient,
                uint64_t amount,
                uint64_t fee,
                uint64_t nonce,
                const Bytes& public_key,
                TransactionType type = TransactionType::STANDARD,
                const Bytes& payload = {},
                SecurityTier tier = SecurityTier::STANDARD_RETAIL);

    /**
     * @brief Returns the hash of the full transaction (including signature).
     * @return A `Bytes` object containing the 32-byte BLAKE3 hash.
     */
    Bytes get_hash() const;

    /**
     * @brief Returns a hash of the transaction data (excluding signature) for signing.
     *
     * This method serializes the core components of the transaction (type, sender, recipient, amount, fee, nonce, payload)
     * into a byte vector and then computes its cryptographic hash (e.g., using BLAKE3). This hash
     * is the data that the sender will sign.
     *
     * @return A `Bytes` object containing the hash of the transaction data.
     */
    Bytes get_hash_for_signing() const;

    /**
     * @brief Serializes the transaction into a byte vector for hashing or network transmission.
     *
     * This method converts all components of the transaction into a contiguous byte array.
     * This is essential for storing the transaction, transmitting it over a network,
     * or for use in cryptographic hashing.
     *
     * @return A `Bytes` object containing the serialized representation of the transaction.
     */
    Bytes serialize() const;

    /**
     * @brief Deserializes a byte vector into a Transaction object.
     *
     * This static method reconstructs a `Transaction` object from its serialized byte representation.
     * It is the inverse operation of `serialize()`.
     *
     * @param data A `Bytes` object containing the serialized transaction data.
     * @return A `Transaction` object reconstructed from the input data.
     */
    static Transaction deserialize(const Bytes& data);

    /**
     * @brief Converts the transaction to a JSON object.
     *
     * This method serializes the transaction's data into a human-readable
     * JSON format, which is useful for API responses, logging, or debugging.
     *
     * @return A `nlohmann::json` object representing the transaction.
     */
    nlohmann::json to_json() const;

    /**
     * @brief Converts the transaction to a string representation (JSON format).
     *
     * This method serializes the transaction's data into a human-readable
     * JSON string, which is useful for logging or debugging.
     *
     * @return A `std::string` representing the transaction in JSON format.
     */
    std::string to_string() const;

    /**
     * @brief Populates the transaction from a JSON object.
     *
     * This method deserializes a JSON object and uses its content to populate the
     * fields of the `Transaction` object. This is the inverse operation of `to_json()`.
     *
     * @param j A `nlohmann::json` object containing the transaction's data.
     */
    void from_json(const nlohmann::json& j);

    /**
     * @brief Checks if the transaction is valid.
     *
     * This method performs various validation checks to ensure the integrity and correctness
     * of the transaction. This typically includes checking for potential overflows when
     * summing amount and fee, and ensuring addresses are valid.
     *
     * @return `true` if the transaction is valid according to the defined rules, `false` otherwise.
     */
    bool is_valid() const;
};

} // namespace chrono_ledger