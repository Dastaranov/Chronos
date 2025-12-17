//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file signature.hpp
 * @brief Defines the Signature class for handling cryptographic signatures.
 *
 * This file provides the definition of the `Signature` class, a wrapper around
 * a byte array (`chrono_util::Bytes`) that represents a cryptographic signature.
 * It offers convenient methods for converting signatures to and from hexadecimal
 * string representations.
 */

#pragma once
#include "util/bytes.hpp"
#include <string>

namespace chrono_crypto {

/**
 * @class Signature
 * @brief Represents a cryptographic signature as a byte array.
 *
 * The `Signature` class encapsulates the raw byte data of a cryptographic signature.
 * It provides constructors for easy initialization and utility methods for
 * hexadecimal encoding and decoding of the signature data, facilitating storage
 * and transmission.
 */
class Signature {
public:
    ///< @var The raw byte data of the cryptographic signature.
    Bytes data;

    /**
     * @brief Default constructor for the Signature class.
     *
     * Initializes an empty Signature object. The `data` member will be empty.
     */
    Signature() = default;

    /**
     * @brief Constructs a Signature from a Bytes object.
     *
     * Initializes a Signature object with the provided raw byte data.
     *
     * @param data A `chrono_util::Bytes` object containing the raw signature data.
     */
    explicit Signature(Bytes data) : data(std::move(data)) {}

    /**
     * @brief Creates a Signature object from a hexadecimal string.
     *
     * Converts a hexadecimal string representation of a signature into a `Signature` object.
     *
     * @param hex_str The hexadecimal string to convert.
     * @return A `Signature` object populated with the decoded byte data.
     */
    static Signature from_hex(const std::string& hex_str) {
        return Signature(chrono_util::hex_to_bytes(hex_str));
    }

    /**
     * @brief Converts the Signature to its hexadecimal string representation.
     *
     * @return A string containing the hexadecimal representation of the signature's byte data.
     */
    std::string to_hex() const {
        return chrono_util::bytes_to_hex(data);
    }
};

} // namespace chrono_crypto
