//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bech32m.hpp
 * @brief This file provides the declaration of functions for bech32m encoding and decoding.
 *
 * Bech32m is a modern and robust encoding scheme used for representing binary data in a human-readable format.
 * It is an improved version of bech32, offering better error detection. This file declares the primary functions
 * for handling bech32m operations:
 * - bech32m_encode: Converts a byte vector into a bech32m string.
 * - bech32m_decode: Converts a bech32m string back into its original byte vector and human-readable part (HRP).
 */

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <utility> // For std::pair
#include <cstdint> // For uint8_t

namespace chrono_address {

/**
 * @brief Encodes a byte vector into a bech32m string.
 *
 * This function takes a human-readable part (HRP) and a vector of bytes (data) and encodes them
 * into a bech32m string. The HRP is a prefix that identifies the type of data being encoded.
 * The data is converted into 5-bit groups, and a checksum is appended to ensure data integrity.
 *
 * @param hrp The human-readable part of the bech32m string. This typically indicates the data's purpose.
 * @param data The byte vector to be encoded. Each byte is treated as an 8-bit value.
 * @return The resulting bech32m encoded string.
 */
std::string bech32m_encode(const std::string& hrp, const std::vector<uint8_t>& data);

/**
 * @brief Decodes a bech32m string to retrieve the HRP and data.
 *
 * This function takes a bech32m encoded string and attempts to decode it. If successful, it returns
 * a pair containing the human-readable part (HRP) and the original data as a byte vector.
 * The function performs validation, including checking the checksum and character set, to ensure the
 * integrity of the decoded data.
 *
 * @param bech32m_string The bech32m string to be decoded.
 * @return An std::optional containing a pair of the HRP and data if decoding is successful.
 *         If decoding fails due to an invalid format, checksum, or other error, it returns std::nullopt.
 */
std::optional<std::pair<std::string, std::vector<uint8_t>>> bech32m_decode(const std::string& bech32m_string);

} // namespace chrono_address
