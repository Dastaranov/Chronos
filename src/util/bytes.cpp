//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bytes.cpp
 * @brief This file implements utility functions for handling byte arrays in the Chronos project.
 *
 * This file provides concrete implementations for converting between byte vectors (`chrono_util::Bytes`),
 * hexadecimal string representations, and standard `std::string` objects. These utilities are
 * essential for data serialization, deserialization, and display within the Chronos system.
 *
 * Key functions implemented:
 * - `bytes_to_hex`: Converts a byte vector to a hexadecimal string.
 * - `hex_to_bytes`: Converts a hexadecimal string to a byte vector.
 * - `string_to_bytes`: Converts a standard string to a byte vector.
 * - `bytes_to_string`: Converts a byte vector to a standard string.
 */

#include "util/bytes.hpp"
#include <iomanip> // For std::hex, std::setfill, std::setw
#include <sstream> // For std::stringstream
#include <stdexcept> // For std::runtime_error

namespace chrono_util {

/**
 * @brief Converts a `Bytes` object (byte vector) to its hexadecimal string representation.
 *
 * This function iterates through each byte in the input `Bytes` object. Each byte is
 * converted to an integer, then formatted as a two-character hexadecimal string (e.g., 0x0A becomes "0a").
 * The resulting hexadecimal characters are appended to a `std::stringstream` to build the final string.
 *
 * @param bytes The `Bytes` object to convert.
 * @return A `std::string` containing the hexadecimal representation of the bytes.
 */
std::string bytes_to_hex(const Bytes& bytes) {
    std::stringstream ss; ///< @var ss A stringstream used to build the hexadecimal string.
    ss << std::hex << std::setfill('0'); // Set output to hexadecimal format and pad with leading zeros
    for (uint8_t b : bytes) {
        ss << std::setw(2) << static_cast<int>(b); // Convert byte to int for correct output, ensure two characters
    }
    return ss.str();
}

/**
 * @brief Converts a hexadecimal string representation to a `Bytes` object (byte vector).
 *
 * This function takes a hexadecimal string and converts it into a `Bytes` object.
 * It expects the input string to have an even length, as each byte is represented by two hexadecimal characters.
 * It iterates through the string, taking two characters at a time, converting them to an unsigned long integer
 * (base 16), and then casting to `uint8_t`.
 *
 * @param hex_str The hexadecimal string to convert.
 * @return A `Bytes` object containing the byte representation of the hexadecimal string.
 * @throw std::runtime_error if the hexadecimal string has an odd length.
 */
Bytes hex_to_bytes(const std::string& hex_str) {
    if (hex_str.length() % 2 != 0) {
        throw std::runtime_error("Hex string must have an even length.");
    }
    Bytes bytes; ///< @var bytes The `Bytes` object to store the converted bytes.
    bytes.reserve(hex_str.length() / 2); // Pre-allocate memory for efficiency
    for (size_t i = 0; i < hex_str.length(); i += 2) {
        std::string byte_str = hex_str.substr(i, 2); ///< @var byte_str A two-character substring representing a single byte.
        bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16))); // Convert hex substring to uint8_t
    }
    return bytes;
}

/**
 * @brief Converts a `std::string` to a `Bytes` object (byte vector).
 *
 * This function creates a `Bytes` object directly from the characters of a `std::string`.
 * Each character's ASCII/UTF-8 value is treated as a byte.
 *
 * @param str The `std::string` to convert.
 * @return A `Bytes` object containing the byte representation of the string.
 */
Bytes string_to_bytes(const std::string& str) {
    return Bytes(str.begin(), str.end());
}

/**
 * @brief Converts a `Bytes` object (byte vector) to a `std::string`.
 *
 * This function creates a `std::string` directly from the bytes of a `Bytes` object.
 * Each byte is treated as a character.
 *
 * @param bytes The `Bytes` object to convert.
 * @return A `std::string` containing the string representation of the bytes.
 */
std::string bytes_to_string(const Bytes& bytes) {
    return std::string(bytes.begin(), bytes.end());
}

// Stream operators for Bytes human-readable output (for logging/debugging)
std::ostream& operator<<(std::ostream& os, const Bytes& bytes) {
    return os << bytes_to_hex(bytes);
}

// Utility functions for appending and reading integers in little-endian format
inline void append_u64_le(chrono_util::Bytes& out, uint64_t v) {
    for (int i=0; i<8; ++i) out.push_back(static_cast<uint8_t>(v >> (8*i)));
}
inline uint64_t read_u64_le(const chrono_util::Bytes& in, size_t& off) {
    if (in.size() < off + 8) throw std::runtime_error("u64 read OOB");
    uint64_t v = 0; for (int i=0;i<8;++i) v |= static_cast<uint64_t>(in[off+i]) << (8*i);
    off += 8; return v;
}

// VarInt à la Bitcoin (optioneel)
inline void append_varint(chrono_util::Bytes& out, uint64_t v) { /* zoals besproken */ }
inline uint64_t read_varint(const chrono_util::Bytes& in, size_t& off) { /* zoals besproken */ }


// NOTE: The binary serialization/deserialization logic previously here is removed
// as this operator is intended for human-readable output. Binary (de)serialization
// should be handled explicitly through serialize()/deserialize() methods where needed.
// Example of binary stream operators (if needed separately):
/*
std::ostream& operator<<(std::ostream& os, const Bytes& bytes) {
    size_t size = bytes.size();
    os.write(reinterpret_cast<const char*>(&size), sizeof(size));
    os.write(reinterpret_cast<const char*>(bytes.data()), size);
    return os;
}

std::istream& operator>>(std::istream& is, Bytes& bytes) {
    size_t size;
    is.read(reinterpret_cast<char*>(&size), sizeof(size));
    if (is.gcount() != sizeof(size)) { // Check if read was successful
        is.setstate(std::ios::failbit);
        return is;
    }
    bytes.resize(size);
    is.read(reinterpret_cast<char*>(bytes.data()), size);
    if (is.gcount() != size) { // Check if read was successful
        is.setstate(std::ios::failbit);
    }
    return is;
}
*/

} // namespace chrono_util
