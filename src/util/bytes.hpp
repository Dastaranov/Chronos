//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bytes.hpp
 * @brief This file defines utility functions and a type alias for handling byte arrays in the Chronos project.
 *
 * This header provides a convenient `Bytes` type alias for `std::vector<uint8_t>`,
 * along with a custom hasher for `Bytes` to enable their use in hash-based containers
 * like `std::unordered_map`. It also declares functions for converting between
 * byte arrays, hexadecimal strings, and standard strings.
 *
 * Key functionalities include:
 * - `Bytes`: Type alias for `std::vector<uint8_t>`.
 * - `BytesHasher`: Custom hash function for `Bytes` objects.
 * - `bytes_to_hex(const Bytes& bytes)`: Converts a byte vector to a hexadecimal string.
 * - `hex_to_bytes(const std::string& hex_str)`: Converts a hexadecimal string to a byte vector.
 * - `string_to_bytes(const std::string& str)`: Converts a standard string to a byte vector.
 * - `bytes_to_string(const Bytes& bytes)`: Converts a byte vector to a standard string.
 */

#pragma once
#include <vector>
#include <cstdint> // Voor uint8_t
#include <string> // For std::string
#include <functional> // For std::hash

namespace chrono_util {
    /**
     * @brief Type alias for a vector of unsigned 8-bit integers, representing a byte array.
     *
     * This alias simplifies the use of byte arrays throughout the Chronos codebase,
     * making code more readable and consistent.
     */
    using Bytes = std::vector<uint8_t>;

    /**
     * @brief Custom hasher for `Bytes` objects.
     *
     * This struct provides a hash function for `chrono_util::Bytes` objects,
     * allowing them to be used as keys in `std::unordered_map` and other hash-based
     * containers. It uses a simple XOR-shift hash algorithm.
     */
    struct BytesHasher {
        /**
         * @brief Computes a hash for a `Bytes` object.
         *
         * @param bytes The `Bytes` object to hash.
         * @return The computed hash value.
         */
            std::size_t operator()(const Bytes& bytes) const {
                std::size_t seed = bytes.size(); ///< @var seed Initial seed for the hash, based on the size of the byte vector.
                for (uint8_t x : bytes) {
                    seed ^= x + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                }
                return seed;
            }
        };
        
        // Stream operators for Bytes serialization/deserialization
        std::ostream& operator<<(std::ostream& os, const Bytes& bytes);
        std::istream& operator>>(std::istream& is, Bytes& bytes);
    /**
     * @brief Converts a `Bytes` object (byte vector) to its hexadecimal string representation.
     *
     * Each byte in the input vector is converted into its two-character hexadecimal equivalent.
     *
     * @param bytes The `Bytes` object to convert.
     * @return A `std::string` containing the hexadecimal representation of the bytes.
     */
    std::string bytes_to_hex(const Bytes& bytes);

    /**
     * @brief Converts a hexadecimal string representation to a `Bytes` object (byte vector).
     *
     * The input hexadecimal string is parsed two characters at a time, and each pair
     * is converted back into a byte.
     *
     * @param hex_str The hexadecimal string to convert.
     * @return A `Bytes` object containing the byte representation of the hexadecimal string.
     */
    Bytes hex_to_bytes(const std::string& hex_str);

    /**
     * @brief Converts a `std::string` to a `Bytes` object (byte vector).
     *
     * Each character in the input string is treated as an 8-bit value and placed
     * into the `Bytes` vector.
     *
     * @param str The `std::string` to convert.
     * @return A `Bytes` object containing the byte representation of the string.
     */
    Bytes string_to_bytes(const std::string& str);

    /**
     * @brief Converts a `Bytes` object (byte vector) to a `std::string`.
     *
     * Each byte in the input vector is treated as a character and appended to a
     * `std::string`.
     *
     * @param bytes The `Bytes` object to convert.
     * @return A `std::string` containing the string representation of the bytes.
     */
    std::string bytes_to_string(const Bytes& bytes);

} // namespace chrono_util