//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file transaction.cpp
 * @brief This file implements the Transaction class, representing a transfer of value in the Chronos ledger.
 *
 * The Transaction class implementation includes methods for constructing transactions,
 * generating a hash for signing, and handling serialization/deserialization to and from
 * byte vectors and JSON objects. It also provides a mechanism for validating the transaction's integrity.
 *
 * Key functions implemented:
 * - `Transaction::Transaction`: Constructor for creating new transactions.
 * - `Transaction::get_hash_for_signing`: Computes a hash of the transaction's core data.
 * - `Transaction::serialize`: Converts the transaction into a byte stream.
 * - `Transaction::deserialize`: Reconstructs a transaction from a byte stream.
 * - `Transaction::to_json`: Converts the transaction into a JSON object.
 * - `Transaction::from_json`: Populates the transaction from a JSON object.
 * - `Transaction::is_valid`: Validates the transaction's internal consistency.
 */

#include "ledger/transaction.hpp"
#include "crypto/blake3.hpp"
#include "util/codec.hpp"
#include "util/bytes.hpp" // For bytes_to_hex and hex_to_bytes
#include <nlohmann/json.hpp> // For JSON serialization/deserialization
#include <stdexcept>
#include <cstring> // For memcpy

namespace chrono_ledger {

/**
 * @brief Constructor for creating a new transaction.
 *
 * Initializes a new transaction with the specified sender, recipient, amount, fee, and nonce.
 * It performs initial validation checks to ensure that both sender and recipient addresses are valid,
 * that the transaction has either an amount or a fee (or both), and that the sum of amount and fee
 * does not result in an overflow.
 *
 * @param sender The address of the sender.
 * @param recipient The address of the recipient.
 * @param amount The amount of value to transfer.
 * @param fee The transaction fee.
 * @param nonce The transaction nonce.
 * @throw std::invalid_argument if sender or recipient address is invalid, or if both amount and fee are zero.
 * @throw std::overflow_error if the sum of amount and fee overflows `uint64_t`.
 */
Transaction::Transaction(const chrono_address::Address& sender,
                         const chrono_address::Address& recipient,
                         uint64_t amount,
                         uint64_t fee,
                         uint64_t nonce,
                         const Bytes& public_key,
                         TransactionType type,
                         const Bytes& payload,
                         SecurityTier tier)
    : sender(sender), recipient(recipient), amount(amount), fee(fee), public_key(public_key), payload(payload), nonce(nonce), tier(tier), type(type) {
    if (!sender.is_valid() || !recipient.is_valid()) {
        throw std::invalid_argument("Sender or recipient address is invalid.");
    }
    if (amount == 0 && fee == 0 && type == TransactionType::TRANSFER) {
        throw std::invalid_argument("Transfer transaction must have amount or fee.");
    }
    // Check for overflow when summing amount and fee
    if (amount > (UINT64_MAX - fee)) { 
        throw std::overflow_error("Amount + fee overflowed.");
    }
}

/**
 * @brief Returns a hash of the transaction data (excluding signature) for signing.
 *
 * Canonical hash format:
 * - type (1 byte)
 * - sender_address_bytes (20 bytes fixed)
 * - recipient_address_bytes (20 bytes fixed)
 * - amount (uint64_t LE)
 * - fee (uint64_t LE)
 * - payload (variable)
 * - tier (1 byte)
 * - nonce (uint64_t LE)
 *
 * @return A `Bytes` object containing the 32-byte BLAKE3 hash of the transaction data.
 */
Bytes Transaction::get_hash_for_signing() const {
    Bytes data_to_hash;
    
    // Add type (1 byte)
    data_to_hash.push_back(static_cast<uint8_t>(type));

    // Add sender address bytes (fixed 20 bytes)
    const auto& sender_bytes = sender.get_bytes();
    data_to_hash.insert(data_to_hash.end(), sender_bytes.begin(), sender_bytes.end());
    
    // Add recipient address bytes (fixed 20 bytes)
    const auto& recipient_bytes = recipient.get_bytes();
    data_to_hash.insert(data_to_hash.end(), recipient_bytes.begin(), recipient_bytes.end());

    // Add amount (uint64_t LE)
    chrono_util::write_fixed_uint64_le(amount, data_to_hash);

    // Add fee (uint64_t LE)
    chrono_util::write_fixed_uint64_le(fee, data_to_hash);

    // Add public key (variable length, but usually fixed for a scheme)
    // We should prefix with length to be safe and canonical
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(public_key.size()), data_to_hash);
    data_to_hash.insert(data_to_hash.end(), public_key.begin(), public_key.end());

    // Add payload (raw bytes)
    data_to_hash.insert(data_to_hash.end(), payload.begin(), payload.end());

    // Add security tier (1 byte)
    data_to_hash.push_back(static_cast<uint8_t>(tier));

    // Add nonce (uint64_t LE)
    chrono_util::write_fixed_uint64_le(nonce, data_to_hash);

    return chrono_crypto::blake3(data_to_hash);
}

/**
 * @brief Returns the hash of the full transaction (including signature).
 * @return A `Bytes` object containing the 32-byte BLAKE3 hash.
 */
Bytes Transaction::get_hash() const {
    return chrono_crypto::blake3(serialize());
}

/**
 * @brief Serializes the transaction into a byte vector using canonical format.
 *
 * Canonical serialization format (little-endian):
 * - type (1 byte)
 * - sender_address_bytes (20 bytes fixed)
 * - recipient_address_bytes (20 bytes fixed)
 * - amount (uint64_t LE)
 * - fee (uint64_t LE)
 * - payload_length (uint32_t LE)
 * - payload_bytes (variable)
 * - signature_length (uint32_t LE)
 * - signature_bytes (variable)
 * - tier (1 byte)
 * - nonce (uint64_t LE)
 *
 * @return A `Bytes` object containing the serialized representation of the transaction.
 */
Bytes Transaction::serialize() const {
    Bytes serialized_data;

    // Add type (1 byte)
    serialized_data.push_back(static_cast<uint8_t>(type));

    // Add sender address bytes (fixed 20 bytes)
    const auto& sender_bytes = sender.get_bytes();
    serialized_data.insert(serialized_data.end(), sender_bytes.begin(), sender_bytes.end());

    // Add recipient address bytes (fixed 20 bytes)
    const auto& recipient_bytes = recipient.get_bytes();
    serialized_data.insert(serialized_data.end(), recipient_bytes.begin(), recipient_bytes.end());

    // Add amount (uint64_t LE)
    chrono_util::write_fixed_uint64_le(amount, serialized_data);

    // Add fee (uint64_t LE)
    chrono_util::write_fixed_uint64_le(fee, serialized_data);

    // Add public key with length prefix
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(public_key.size()), serialized_data);
    serialized_data.insert(serialized_data.end(), public_key.begin(), public_key.end());

    // Add payload with length prefix
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(payload.size()), serialized_data);
    serialized_data.insert(serialized_data.end(), payload.begin(), payload.end());

    // Add signature with length prefix (uint32_t LE, not size_t)
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(signature.size()), serialized_data);
    serialized_data.insert(serialized_data.end(), signature.begin(), signature.end());

    // Add security tier (1 byte)
    serialized_data.push_back(static_cast<uint8_t>(tier));

    // Add nonce (uint64_t LE)
    chrono_util::write_fixed_uint64_le(nonce, serialized_data);

    return serialized_data;
}

/**
 * @brief Deserializes a byte vector into a Transaction object.
 *
 * This static method reconstructs a `Transaction` object from its canonical serialized byte representation.
 * It parses the byte array in little-endian format with fixed-width types, extracting each field in order.
 *
 * Expected format:
 * - type (1 byte)
 * - sender_address_bytes (20 bytes fixed)
 * - recipient_address_bytes (20 bytes fixed)
 * - amount (uint64_t LE)
 * - fee (uint64_t LE)
 * - payload_length (uint32_t LE)
 * - payload_bytes (variable)
 * - signature_length (uint32_t LE)
 * - signature_bytes (variable)
 * - tier (1 byte, optional for backward compatibility)
 * - nonce (uint64_t LE)
 *
 * @param data A `Bytes` object containing the serialized transaction data.
 * @return A `Transaction` object reconstructed from the input data.
 * @throw std::runtime_error if the input data is too short or malformed.
 */
Transaction Transaction::deserialize(const Bytes& data) {
    size_t offset = 0;

    // Minimum size check: 1 (type) + 20 (sender) + 20 (recipient) + 8 (amount) + 8 (fee) + 4 (pubkey_len) + 4 (payload_len) + 4 (sig_len) + 8 (nonce)
    const size_t MIN_TX_SIZE = 1 + 20 + 20 + 8 + 8 + 4 + 4 + 4 + 8;
    if (data.size() < MIN_TX_SIZE) {
        throw std::runtime_error("Transaction data too short for deserialization.");
    }

    // Read type (1 byte)
    TransactionType type = static_cast<TransactionType>(data[offset]);
    offset += 1;

    // Read sender address (20 bytes fixed)
    chrono_address::Address sender_addr(Bytes(data.begin() + offset, data.begin() + offset + 20), true);
    offset += 20;

    // Read recipient address (20 bytes fixed)
    chrono_address::Address recipient_addr(Bytes(data.begin() + offset, data.begin() + offset + 20), true);
    offset += 20;

    // Read amount (uint64_t LE)
    uint64_t amount_val = chrono_util::read_fixed_uint64_le(data, offset);

    // Read fee (uint64_t LE)
    uint64_t fee_val = chrono_util::read_fixed_uint64_le(data, offset);

    // Read public key length
    uint32_t pubkey_size = chrono_util::read_fixed_uint32_le(data, offset);
    if (offset + pubkey_size > data.size()) {
        throw std::runtime_error("Transaction data too short for public key.");
    }
    Bytes public_key_val(data.begin() + offset, data.begin() + offset + pubkey_size);
    offset += pubkey_size;

    // Read payload length
    uint32_t payload_size = chrono_util::read_fixed_uint32_le(data, offset);
    if (offset + payload_size > data.size()) {
        throw std::runtime_error("Transaction data too short for payload.");
    }
    Bytes payload_val(data.begin() + offset, data.begin() + offset + payload_size);
    offset += payload_size;

    // Read signature length (uint32_t LE, not size_t)
    uint32_t sig_size = chrono_util::read_fixed_uint32_le(data, offset);

    // Check if there's enough data for the signature and nonce
    if (offset + sig_size + 8 > data.size()) {
        throw std::runtime_error("Transaction data too short for signature or nonce.");
    }

    // Read signature bytes
    Bytes signature_val(data.begin() + offset, data.begin() + offset + sig_size);
    offset += sig_size;

    // Read optional security tier (backward-compatible decoding)
    SecurityTier tier_val = SecurityTier::STANDARD_RETAIL;
    if (offset + 9 == data.size()) {
        uint8_t raw_tier = data[offset++];
        if (raw_tier == static_cast<uint8_t>(SecurityTier::CRITICAL_SETTLEMENT)) {
            tier_val = SecurityTier::CRITICAL_SETTLEMENT;
        }
    } else if (offset + 8 != data.size()) {
        throw std::runtime_error("Transaction has malformed trailing bytes.");
    }

    // Read nonce (uint64_t LE)
    uint64_t nonce_val = chrono_util::read_fixed_uint64_le(data, offset);

    // Create and populate transaction
    Transaction tx(sender_addr, recipient_addr, amount_val, fee_val, nonce_val, public_key_val, type, payload_val, tier_val);
    tx.signature = signature_val;
    return tx;
}

/**
 * @brief Converts the transaction to a JSON object.
 *
 * This method serializes the transaction's data into a human-readable
 * `nlohmann::json` object. Addresses are converted to their string representation,
 * and the signature (a byte array) is converted to a hexadecimal string.
 *
 * @return A `nlohmann::json` object representing the transaction.
 */
std::string Transaction::to_string() const {
    return to_json().dump();
}

/**
 * @brief Converts the transaction to a JSON object.
 *
 * This method serializes the transaction's data into a human-readable
 * `nlohmann::json` object. Addresses are converted to their string representation,
 * and the signature (a byte array) is converted to a hexadecimal string.
 *
 * @return A `nlohmann::json` object representing the transaction.
 */
nlohmann::json Transaction::to_json() const {
    nlohmann::json j;
    j["type"] = static_cast<uint8_t>(type);
    j["sender"] = sender.to_string(); // Assuming Address has a to_string() method
    j["recipient"] = recipient.to_string(); // Assuming Address has a to_string() method
    j["amount"] = amount;
    j["fee"] = fee;
    j["public_key"] = chrono_util::bytes_to_hex(public_key);
    j["payload"] = chrono_util::bytes_to_hex(payload);
    j["signature"] = chrono_util::bytes_to_hex(signature);
    j["nonce"] = nonce;
    j["tier"] = static_cast<uint8_t>(tier);
    return j;
}

/**
 * @brief Populates the transaction from a JSON object.
 *
 * This method deserializes a `nlohmann::json` object and uses its content to populate the
 * fields of the `Transaction` object. Address strings are used to construct `Address` objects,
 * and the hexadecimal signature string is converted back to a byte array.
 *
 * @param j A `nlohmann::json` object containing the transaction's data.
 */
void Transaction::from_json(const nlohmann::json& j) {
    if (j.contains("type")) {
        type = static_cast<TransactionType>(j.at("type").get<uint8_t>());
    } else {
        type = TransactionType::TRANSFER;
    }
    sender = chrono_address::Address(j.at("sender").get<std::string>()); // Assuming Address has a constructor from string
    recipient = chrono_address::Address(j.at("recipient").get<std::string>()); // Assuming Address has a constructor from string
    amount = j.at("amount").get<uint64_t>();
    fee = j.at("fee").get<uint64_t>();
    if (j.contains("public_key")) {
        public_key = chrono_util::hex_to_bytes(j.at("public_key").get<std::string>());
    }
    if (j.contains("payload")) {
        payload = chrono_util::hex_to_bytes(j.at("payload").get<std::string>());
    }
    signature = chrono_util::hex_to_bytes(j.at("signature").get<std::string>());
    nonce = j.at("nonce").get<uint64_t>();
    tier = static_cast<SecurityTier>(j.value("tier", static_cast<uint8_t>(SecurityTier::STANDARD_RETAIL)));
}

/**
 * @brief Checks if the transaction is valid.
 *
 * This method performs various validation checks to ensure the internal consistency
 * and correctness of the transaction. It verifies that both sender and recipient addresses
 * are valid, that the transaction has a non-zero amount or fee, and that the sum of
 * amount and fee does not overflow `uint64_t`.
 *
 * @return `true` if the transaction passes all internal validation checks, `false` otherwise.
 */
bool Transaction::is_valid() const {
    const uint8_t raw_tier = static_cast<uint8_t>(tier);
    const bool tier_valid = (raw_tier == static_cast<uint8_t>(SecurityTier::STANDARD_RETAIL) ||
                             raw_tier == static_cast<uint8_t>(SecurityTier::CRITICAL_SETTLEMENT));
    return sender.is_valid() && recipient.is_valid() && (amount > 0 || fee > 0) && (amount + fee >= amount) && tier_valid;
}

} // namespace chrono_ledger