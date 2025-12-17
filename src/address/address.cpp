//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file address.cpp
 * @brief This file implements the Address class, which is responsible for creating, managing, and validating Chronos addresses.
 *
 * The implementation of the Address class includes constructors for creating addresses from public keys and bech32m strings,
 * as well as methods for accessing the address data and performing validation. The main functions are:
 * - Address(const Bytes& public_key): Hashes a public key to generate a 20-byte address and encodes it into a bech32m string.
 * - Address(const std::string& bech32m_address): Decodes a bech32m string to reconstruct the address bytes.
 * - get_bytes(): Returns the raw 20-byte address.
 * - to_string(): Returns the bech32m encoded string.
 * - is_valid(): Checks if the address is properly initialized.
 */

#include "address/address.hpp"
#include "crypto/blake3.hpp"
#include "util/bytes.hpp"
using chrono_util::Bytes;
#include <stdexcept>

namespace chrono_address {

/**
 * @brief The human-readable part (HRP) for Chronos bech32m addresses.
 *
 * This constant defines the prefix used for all Chronos addresses, which helps in identifying
 * the address type and preventing errors. "cqc" stands for Chronos Coin.
 */
const std::string Address::HRP = "cqc"; // Chronos Coin

/**
 * @brief Constructs an Address from a public key.
 *
 * This constructor initializes an Address object from a given public key. It first validates that the public key is not empty.
 * Then, it computes the BLAKE3 hash of the public key and truncates it to the first 20 bytes to form the address.
 * Finally, it encodes the resulting address bytes into a bech32m string, which is cached for future use.
 *
 * @param public_key The public key from which to derive the address. It is represented as a Bytes object.
 * @throw std::invalid_argument if the provided public key is empty.
 * @throw std::runtime_error if the BLAKE3 hash is shorter than the required 20 bytes.
 */
Address::Address(const Bytes& public_key) {
    if (public_key.empty()) {
        throw std::invalid_argument("Public key cannot be empty.");
    }
    // Hash the public key using BLAKE3
    Bytes full_hash = chrono_crypto::blake3(public_key);

    // Take the first 20 bytes for the address (160-bit address)
    if (full_hash.size() < 20) {
        throw std::runtime_error("BLAKE3 hash too short for 20-byte address.");
    }
    address_bytes.assign(full_hash.begin(), full_hash.begin() + 20);

    // Encode to bech32m string
    bech32m_string = bech32m_encode(HRP, address_bytes);
}

/**
 * @brief Constructs an Address from a bech32m string.
 *
 * This constructor initializes an Address object from a bech32m encoded string. It decodes the string to
 * retrieve the raw address bytes. It performs several checks to ensure the validity of the bech32m string,
 * including verifying the human-readable part (HRP) and ensuring the data part is exactly 20 bytes long.
 *
 * @param bech32m_address The bech32m encoded address string.
 * @throw std::invalid_argument if the bech32m string is invalid, has an incorrect HRP, or the data part is not 20 bytes.
 */
Address::Address(const std::string& bech32m_address) : bech32m_string(bech32m_address) {
    auto decoded = bech32m_decode(bech32m_address);
    if (!decoded || decoded->first != HRP || decoded->second.size() != 20) {
        throw std::invalid_argument("Invalid bech32m address string.");
    }
    address_bytes = decoded->second;
}

/**
 * @brief Returns the raw address bytes.
 *
 * This method provides access to the 20-byte raw data of the address, which is the truncated
 * BLAKE3 hash of the original public key.
 *
 * @return A constant reference to the Bytes object containing the address bytes.
 */
const Bytes& Address::get_bytes() const {
    return address_bytes;
}

/**
 * @brief Returns the bech32m string representation of the address.
 *
 * This method returns the cached bech32m encoded string of the address. This string is generated
 * at the time of address creation and is suitable for user-facing display.
 *
 * @return The bech32m string representation of the address.
 */
std::string Address::to_string() const {
    return bech32m_string;
}

/**
 * @brief Checks if the address is valid.
 *
 * An address is considered valid if both its raw byte representation and its bech32m string
 * are not empty. This ensures that the Address object has been successfully initialized.
 *
 * @return true if the address is valid, false otherwise.
 */
bool Address::is_valid() const {
    return !address_bytes.empty() && !bech32m_string.empty();
}

/**
 * @brief Static method to validate a bech32m address string.
 *
 * Validates an address string without constructing an Address object. This is useful for
 * pre-validation before attempting to create an Address.
 *
 * @param addr_str The bech32m encoded address string to validate.
 * @return true if the address string is valid, false otherwise.
 */
bool Address::is_valid(const std::string& addr_str) {
    if (addr_str.empty()) {
        return false;
    }
    
    try {
        auto decoded = bech32m_decode(addr_str);
        // Check HRP matches and data is exactly 20 bytes
        if (!decoded || decoded->first != HRP || decoded->second.size() != 20) {
            return false;
        }
        return true;
    } catch (...) {
        return false;
    }
}

/**
 * @brief Equality comparison operator.
 *
 * This operator compares two Address objects for equality by comparing their raw address bytes.
 *
 * @param other The other Address object to compare against.
 * @return true if the address bytes are identical, false otherwise.
 */
bool Address::operator==(const Address& other) const {
    return address_bytes == other.address_bytes;
}

/**
 * @brief Inequality comparison operator.
 *
 * This operator checks if two Address objects are not equal by using the result of the
 * equality operator.
 *
 * @param other The other Address object to compare against.
 * @return true if the addresses are different, false otherwise.
 */
bool Address::operator!=(const Address& other) const {
    return !(*this == other);
}

} // namespace chrono_address