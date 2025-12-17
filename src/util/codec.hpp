//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file codec.hpp
 * @brief Provides canonical serialization helpers for wire format data.
 *
 * This header defines utilities for serializing and deserializing numeric types
 * in a canonical format using little-endian byte order and variable-length integer
 * encoding (VarInt) for efficient storage of variable-length integers.
 *
 * Canonical format requirements:
 * - All multi-byte integers use little-endian (LE) byte order
 * - Fixed-width types use explicit uint32_t or uint64_t (no size_t)
 * - Variable-length integers use VarInt encoding for counts/lengths
 * - All operations preserve deterministic serialization for hashing
 */

#pragma once

#include "util/bytes.hpp"
#include <cstring>
#include <cstdint>

namespace chrono_util {

/**
 * @brief Write a 32-bit unsigned integer in little-endian format.
 *
 * @param value The value to write
 * @param out The output bytes vector (will be resized/appended)
 */
inline void write_fixed_uint32_le(uint32_t value, Bytes& out) {
    uint8_t bytes[4];
    bytes[0] = (value >> 0) & 0xFF;
    bytes[1] = (value >> 8) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 24) & 0xFF;
    out.insert(out.end(), bytes, bytes + 4);
}

/**
 * @brief Write a 64-bit unsigned integer in little-endian format.
 *
 * @param value The value to write
 * @param out The output bytes vector (will be appended)
 */
inline void write_fixed_uint64_le(uint64_t value, Bytes& out) {
    uint8_t bytes[8];
    bytes[0] = (value >> 0) & 0xFF;
    bytes[1] = (value >> 8) & 0xFF;
    bytes[2] = (value >> 16) & 0xFF;
    bytes[3] = (value >> 24) & 0xFF;
    bytes[4] = (value >> 32) & 0xFF;
    bytes[5] = (value >> 40) & 0xFF;
    bytes[6] = (value >> 48) & 0xFF;
    bytes[7] = (value >> 56) & 0xFF;
    out.insert(out.end(), bytes, bytes + 8);
}

/**
 * @brief Read a 32-bit unsigned integer in little-endian format.
 *
 * @param data The input bytes
 * @param offset The offset to read from (will be incremented by 4)
 * @return The decoded 32-bit value
 * @throws std::runtime_error if not enough data available
 */
inline uint32_t read_fixed_uint32_le(const Bytes& data, size_t& offset) {
    if (offset + 4 > data.size()) {
        throw std::runtime_error("Not enough data to read uint32_le");
    }
    uint32_t value = 0;
    value |= static_cast<uint32_t>(data[offset + 0]) << 0;
    value |= static_cast<uint32_t>(data[offset + 1]) << 8;
    value |= static_cast<uint32_t>(data[offset + 2]) << 16;
    value |= static_cast<uint32_t>(data[offset + 3]) << 24;
    offset += 4;
    return value;
}

/**
 * @brief Read a 64-bit unsigned integer in little-endian format.
 *
 * @param data The input bytes
 * @param offset The offset to read from (will be incremented by 8)
 * @return The decoded 64-bit value
 * @throws std::runtime_error if not enough data available
 */
inline uint64_t read_fixed_uint64_le(const Bytes& data, size_t& offset) {
    if (offset + 8 > data.size()) {
        throw std::runtime_error("Not enough data to read uint64_le");
    }
    uint64_t value = 0;
    value |= static_cast<uint64_t>(data[offset + 0]) << 0;
    value |= static_cast<uint64_t>(data[offset + 1]) << 8;
    value |= static_cast<uint64_t>(data[offset + 2]) << 16;
    value |= static_cast<uint64_t>(data[offset + 3]) << 24;
    value |= static_cast<uint64_t>(data[offset + 4]) << 32;
    value |= static_cast<uint64_t>(data[offset + 5]) << 40;
    value |= static_cast<uint64_t>(data[offset + 6]) << 48;
    value |= static_cast<uint64_t>(data[offset + 7]) << 56;
    offset += 8;
    return value;
}

/**
 * @brief Write a variable-length unsigned integer (VarInt).
 *
 * VarInt encoding:
 * - 0x00-0x7F: single byte (0-127)
 * - 0x80-0xFE: two bytes, first byte 0x80 + (value >> 8), second byte (value & 0xFF)
 * - Values >= 0xFF: encoded as 0xFF followed by 8 bytes little-endian uint64_t
 *
 * This is a simple varint format optimized for small values.
 *
 * @param value The value to encode
 * @param out The output bytes vector (will be appended)
 */
inline void write_varint(uint64_t value, Bytes& out) {
    if (value < 0x80) {
        // Single byte
        out.push_back(static_cast<uint8_t>(value));
    } else if (value < 0x4000) {
        // Two bytes: 0x80 + high byte, low byte
        out.push_back(0x80 | static_cast<uint8_t>(value >> 8));
        out.push_back(static_cast<uint8_t>(value & 0xFF));
    } else {
        // Eight bytes: 0xFF marker + uint64_le
        out.push_back(0xFF);
        write_fixed_uint64_le(value, out);
    }
}

/**
 * @brief Read a variable-length unsigned integer (VarInt).
 *
 * @param data The input bytes
 * @param offset The offset to read from (will be incremented appropriately)
 * @return The decoded value
 * @throws std::runtime_error if format is invalid or not enough data
 */
inline uint64_t read_varint(const Bytes& data, size_t& offset) {
    if (offset >= data.size()) {
        throw std::runtime_error("Not enough data to read varint");
    }

    uint8_t first_byte = data[offset];
    
    if (first_byte < 0x80) {
        // Single byte value
        offset++;
        return static_cast<uint64_t>(first_byte);
    } else if (first_byte < 0xFF) {
        // Two byte value
        if (offset + 2 > data.size()) {
            throw std::runtime_error("Not enough data to read two-byte varint");
        }
        uint64_t high = static_cast<uint64_t>(first_byte & 0x7F);
        uint64_t low = static_cast<uint64_t>(data[offset + 1]);
        offset += 2;
        return (high << 8) | low;
    } else {
        // Eight byte value (first_byte == 0xFF)
        offset++;
        return read_fixed_uint64_le(data, offset);
    }
}

/**
 * @brief Write a byte array with length prefix.
 *
 * Writes the length as a uint32_t LE, followed by the bytes.
 *
 * @param bytes The bytes to write
 * @param out The output bytes vector (will be appended)
 */
inline void write_bytes_with_length(const Bytes& bytes, Bytes& out) {
    write_fixed_uint32_le(static_cast<uint32_t>(bytes.size()), out);
    out.insert(out.end(), bytes.begin(), bytes.end());
}

/**
 * @brief Read a byte array with length prefix.
 *
 * Reads a uint32_t LE length, then reads that many bytes.
 *
 * @param data The input bytes
 * @param offset The offset to read from (will be incremented)
 * @return The decoded bytes
 * @throws std::runtime_error if not enough data available
 */
inline Bytes read_bytes_with_length(const Bytes& data, size_t& offset) {
    uint32_t length = read_fixed_uint32_le(data, offset);
    if (offset + length > data.size()) {
        throw std::runtime_error("Not enough data to read bytes");
    }
    Bytes result(data.begin() + offset, data.begin() + offset + length);
    offset += length;
    return result;
}

/**
 * @brief Write a string with length prefix.
 *
 * Writes the length as a uint32_t LE, followed by UTF-8 bytes.
 *
 * @param str The string to write
 * @param out The output bytes vector (will be appended)
 */
inline void write_string_with_length(const std::string& str, Bytes& out) {
    write_fixed_uint32_le(static_cast<uint32_t>(str.size()), out);
    out.insert(out.end(), str.begin(), str.end());
}

/**
 * @brief Read a string with length prefix.
 *
 * Reads a uint32_t LE length, then reads that many bytes as a string.
 *
 * @param data The input bytes
 * @param offset The offset to read from (will be incremented)
 * @return The decoded string
 * @throws std::runtime_error if not enough data available
 */
inline std::string read_string_with_length(const Bytes& data, size_t& offset) {
    uint32_t length = read_fixed_uint32_le(data, offset);
    if (offset + length > data.size()) {
        throw std::runtime_error("Not enough data to read string");
    }
    std::string result(data.begin() + offset, data.begin() + offset + length);
    offset += length;
    return result;
}

} // namespace chrono_util
