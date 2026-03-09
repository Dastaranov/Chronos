//
// Created by Dastaranov | 2025 / 2026 | Belgium
//

/**
 * @file address.hpp
 * @brief This file defines the Address class, which is responsible for creating, managing, and validating Chronos addresses.
 *
 * The Address class encapsulates the logic for handling Chronos addresses. It supports creating addresses from public keys,
 * converting them to and from bech32m format, and validating their integrity. The primary functions of this file are:
 * - Address(const Bytes& public_key): Constructs an address from a given public key by hashing it.
 * - Address(const std::string& bech32m_address): Constructs an address from a bech32m encoded string.
 * - get_bytes(): Retrieves the raw byte representation of the address.
 * - to_string(): Converts the address to its bech32m string representation.
 * - is_valid(): Checks if the address is valid.
 */

#pragma once

#include <string>
#include "util/bytes.hpp"
using chrono_util::Bytes;
#include "crypto/blake3.hpp" // For hashing public key
#include "address/bech32m.hpp" // For encoding/decoding

namespace chrono_address {

/**
 * @class Address
 * @brief Represents a Chronos address, handling its creation, validation, and conversion.
 *
 * This class manages the lifecycle of a Chronos address. It can be instantiated from a public key,
 * which is then hashed to create the address, or from a bech32m string. It provides methods
 * to access the raw address bytes and its string representation, as well as to validate the address.
 */
class Address {
public:
    /**
     * @brief Default constructor for the Address class.
     *
     * Initializes an empty Address object. This address will be marked as invalid
     * until it is properly initialized, for example, from a public key or a bech32m string.
     */
    Address() = default;

    /**
     * @brief Constructs an Address from a public key.
     *
     * This constructor takes a public key as a byte array, hashes it using the BLAKE3 algorithm,
     * and then truncates the hash to 20 bytes to form the address bytes. It also pre-computes
     * the bech32m string representation of the address.
     *
     * @param public_key The public key represented as a Bytes object. This key is used to generate the address.
     */
    explicit Address(const Bytes& public_key);

    /**
     * @brief Constructs an Address from raw address bytes.
     * 
     * This constructor takes the raw 20-byte address directly. It validates the size
     * and generates the bech32m string.
     * 
     * @param bytes The 20-byte raw address.
     * @param is_raw_bytes Tag to distinguish from public key constructor (value ignored).
     */
    Address(const Bytes& bytes, bool is_raw_bytes);

    /**
     * @brief Constructs an Address from a bech32m string.
     *
     * This constructor takes a bech32m encoded string and decodes it to retrieve the raw address bytes.
     * It validates the format and checksum of the bech32m string. If the string is valid, the
     * address bytes and the bech32m string are stored in the object.
     *
     * @param bech32m_address The bech32m encoded address string.
     */
    explicit Address(const std::string& bech32m_address);

    /**
     * @brief Returns the raw address bytes.
     *
     * This method provides access to the raw 20-byte hash of the public key that constitutes the address.
     *
     * @return A constant reference to the Bytes object containing the address bytes.
     */
    const Bytes& get_bytes() const;

    /**
     * @brief Returns the bech32m string representation of the address.
     *
     * This method returns the bech32m encoded string of the address. The bech32m format is a
     * human-readable format that includes a checksum to prevent errors.
     *
     * @return The bech32m string representation of the address.
     */
    std::string to_string() const;

    /**
     * @brief Checks if the address is valid.
     *
     * An address is considered valid if its byte representation is not empty. This method can be used
     * to verify that an Address object has been properly initialized.
     *
     * @return true if the address is valid, false otherwise.
     */
    bool is_valid() const;

    /**
     * @brief Static method to validate a bech32m address string.
     *
     * Validates an address string without constructing an Address object.
     * Checks if the string is a properly formatted bech32m address.
     *
     * @param addr_str The bech32m encoded address string to validate.
     * @return true if the address string is valid, false otherwise.
     */
    static bool is_valid(const std::string& addr_str);

    /**
     * @brief Equality comparison operator.
     *
     * Compares this Address with another Address object for equality. The comparison is based on the
     * raw address bytes.
     *
     * @param other The other Address object to compare with.
     * @return true if the addresses are identical, false otherwise.
     */
    bool operator==(const Address& other) const;

    /**
     * @brief Inequality comparison operator.
     *
     * Compares this Address with another Address object for inequality. The comparison is based on the
     * raw address bytes.
     *
     * @param other The other Address object to compare with.
     * @return true if the addresses are different, false otherwise.
     */
    bool operator!=(const Address& other) const;

private:
    Bytes address_bytes; ///< @var The 20-byte BLAKE3 hash of the public key. This is the core data of the address.
    std::string bech32m_string; ///< @var Cached bech32m string representation of the address. This is stored for efficiency.
    static const std::string HRP; ///< @var The human-readable part for bech32m encoding, typically a prefix for the address.
};

} // namespace chrono_address