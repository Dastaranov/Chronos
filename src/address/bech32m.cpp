//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bech32m.cpp
 * @brief This file provides the implementation of functions for bech32m encoding and decoding.
 *
 * The bech32m format is a robust encoding scheme that includes a checksum for error detection, making it ideal for
 * representing sensitive data like cryptocurrency addresses. This implementation includes the core logic for:
 * - `bech32m_encode`: Converts binary data into a human-readable bech32m string.
 * - `bech32m_decode`: Validates and converts a bech32m string back into its binary form.
 * The file also contains several helper functions within an anonymous namespace to support these operations,
 * such as bit conversion, checksum calculation, and HRP expansion.
 */

#include "address/bech32m.hpp"
#include <vector>
#include <string>
#include <numeric>
#include <algorithm>
#include <stdexcept>

namespace chrono_address {

namespace { // Anonymous namespace for helper functions

/**
 * @brief The character set used for bech32m encoding.
 *
 * This string defines the 32 characters that are used to represent the 5-bit groups in a bech32m string.
 * The character set is chosen to be easily readable and to minimize the risk of ambiguous characters.
 */
const std::string CHARSET = "qpzry9x8gf2tvdw0s3jn54khce6mua7l";

/**
 * @brief Converts a vector of bytes from one bit-width to another.
 *
 * This function is a key part of the bech32m process. It re-packs data from a source bit-width (`from_bits`)
 * to a target bit-width (`to_bits`). For encoding, it converts 8-bit bytes to 5-bit groups. For decoding,
 * it does the reverse.
 *
 * @param data The input data to convert, as a vector of bytes.
 * @param from_bits The bit-width of the input data (e.g., 8 for bytes).
 * @param to_bits The desired bit-width of the output data (e.g., 5 for bech32m).
 * @param pad A boolean indicating whether to pad the output to a full byte. This is true for encoding.
 * @return A vector of bytes representing the converted data.
 * @throw std::runtime_error if invalid padding is detected during conversion.
 */
std::vector<uint8_t> convert_bits(const std::vector<uint8_t>& data, int from_bits, int to_bits, bool pad) {
    std::vector<uint8_t> acc;
    int bits = 0;
    int value = 0;
    for (uint8_t d : data) {
        value = (value << from_bits) | d;
        bits += from_bits;
        while (bits >= to_bits) {
            bits -= to_bits;
            acc.push_back((value >> bits) & ((1 << to_bits) - 1));
        }
    }
    if (pad) {
        if (bits > 0) {
            acc.push_back((value << (to_bits - bits)) & ((1 << to_bits) - 1));
        }
    } else if (bits >= from_bits || ((value << (to_bits - bits)) & ((1 << to_bits) - 1))) {
        throw std::runtime_error("Invalid padding in convert_bits");
    }
    return acc;
}

/**
 * @brief The polynomial modulus function for bech32m checksum calculation.
 *
 * This function computes the checksum of a bech32m string by processing its values through a series of XOR
 * operations with predefined generator polynomials. The final result is used to verify the integrity of the data.
 *
 * @param values The combined HRP and data values to be checksummed.
 * @return The calculated checksum as a 32-bit unsigned integer.
 */
uint32_t polymod(const std::vector<uint8_t>& values) {
    uint32_t chk = 1;
    const uint32_t GENERATOR[] = {0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3};
    for (uint8_t v : values) {
        uint8_t b = chk >> 25;
        chk = (chk & 0x1ffffff) << 5 ^ v;
        for (int i = 0; i < 5; ++i) {
            if ((b >> i) & 1) {
                chk ^= GENERATOR[i];
            }
        }
    }
    return chk;
}

/**
 * @brief Expands the human-readable part (HRP) for checksum calculation.
 *
 * Before calculating the checksum, the HRP is expanded into a sequence of 5-bit groups. Each character of the
 * HRP is split into its high and low 5 bits, which are then used in the `polymod` function.
 *
 * @param hrp The human-readable part of the bech32m string.
 * @return A vector of bytes representing the expanded HRP.
 */
std::vector<uint8_t> hrp_expand(const std::string& hrp) {
    std::vector<uint8_t> ret;
    ret.reserve(hrp.length() * 2 + 1);
    for (char c : hrp) {
        ret.push_back(static_cast<uint8_t>(c >> 5));
    }
    ret.push_back(0);
    for (char c : hrp) {
        ret.push_back(static_cast<uint8_t>(c & 0x1f));
    }
    return ret;
}

/**
 * @brief Creates the checksum for a bech32m string.
 *
 * This function computes the 6-character checksum that is appended to the data part of a bech32m string.
 * It combines the expanded HRP and the data, pads it with six zero bytes, and then runs it through the
 * `polymod` function. The result is XORed with the bech32m constant to produce the final checksum.
 *
 * @param hrp The human-readable part of the string.
 * @param data The data payload, already converted to 5-bit groups.
 * @return A vector of 6 bytes representing the bech32m checksum.
 */
std::vector<uint8_t> create_checksum(const std::string& hrp, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> values = hrp_expand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    values.push_back(0);
    values.push_back(0);
    values.push_back(0);
    values.push_back(0);
    values.push_back(0);
    values.push_back(0);
    uint32_t poly = polymod(values) ^ 0x2bc830a3; // XOR with BECH32M_CONST
    std::vector<uint8_t> checksum;
    checksum.reserve(6);
    for (int i = 0; i < 6; ++i) {
        checksum.push_back((poly >> (5 * (5 - i))) & 0x1f);
    }
    return checksum;
}

/**
 * @brief Verifies the checksum of a bech32m string.
 *
 * To verify a bech32m string, the expanded HRP and the data (including the checksum) are passed to the
 * `polymod` function. If the result equals the bech32m constant, the checksum is valid.
 *
 * @param hrp The human-readable part of the string.
 * @param data The data payload, including the checksum.
 * @return `true` if the checksum is valid, `false` otherwise.
 */
bool verify_checksum(const std::string& hrp, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> values = hrp_expand(hrp);
    values.insert(values.end(), data.begin(), data.end());
    return polymod(values) == 0x2bc830a3; // Compare with BECH32M_CONST
}

} // End anonymous namespace

/**
 * @brief Encodes a byte vector into a bech32m string.
 *
 * This function orchestrates the bech32m encoding process. It first converts the input data from 8-bit bytes
 * to 5-bit groups. Then, it creates the checksum and appends it to the converted data. Finally, it constructs
 * the full bech32m string by combining the HRP, the separator '1', and the data payload, with each 5-bit group
 * mapped to a character from the bech32m character set.
 *
 * @param hrp The human-readable part of the bech32m string.
 * @param data The byte vector to be encoded.
 * @return The resulting bech32m encoded string.
 */
std::string bech32m_encode(const std::string& hrp, const std::vector<uint8_t>& data) {
    std::vector<uint8_t> combined = convert_bits(data, 8, 5, true);
    std::vector<uint8_t> checksum = create_checksum(hrp, combined);
    combined.insert(combined.end(), checksum.begin(), checksum.end());

    std::string ret = hrp + '1';
    for (uint8_t d : combined) {
        ret += CHARSET[d];
    }
    return ret;
}

/**
 * @brief Decodes a bech32m string to retrieve the HRP and data.
 *
 * This function handles the decoding of a bech32m string. It performs a series of validation checks:
 * - Ensures all characters are within the valid ASCII range.
 * - Prohibits mixed case (all lowercase or all uppercase is required).
 * - Locates the '1' separator and separates the HRP from the data part.
 * - Validates the characters in the HRP and data parts.
 * - Verifies the checksum to ensure data integrity.
 * If all checks pass, it converts the 5-bit data groups back to 8-bit bytes and returns the HRP and data.
 *
 * @param bech32m_string The bech32m string to be decoded.
 * @return An `std::optional` containing a pair of the HRP and data if decoding is successful.
 *         If any validation check fails, it returns `std::nullopt`.
 */
std::optional<std::pair<std::string, std::vector<uint8_t>>> bech32m_decode(const std::string& bech32m_string) {
    bool lower = false, upper = false;
    for (char c : bech32m_string) {
        if (c < 33 || c > 126) return std::nullopt; // Invalid character
        if (c >= 'a' && c <= 'z') lower = true;
        if (c >= 'A' && c <= 'Z') upper = true;
    }
    if (lower && upper) return std::nullopt; // Mixed case

    size_t pos = bech32m_string.rfind('1');
    if (pos == std::string::npos || pos == 0 || pos + 7 > bech32m_string.length()) {
        return std::nullopt; // No '1', '1' at start, or too short
    }

    std::string hrp = bech32m_string.substr(0, pos);
    std::string data_str = bech32m_string.substr(pos + 1);

    for (char c : hrp) {
        if (c < 33 || c > 126) return std::nullopt; // Invalid HRP character
    }

    std::vector<uint8_t> data;
    data.reserve(data_str.length());
    for (char c : data_str) {
        size_t char_pos = CHARSET.find(c);
        if (char_pos == std::string::npos) return std::nullopt; // Invalid data character
        data.push_back(static_cast<uint8_t>(char_pos));
    }

    if (!verify_checksum(hrp, data)) {
        return std::nullopt; // Checksum failed
    }

    std::vector<uint8_t> decoded_data = convert_bits(
        std::vector<uint8_t>(data.begin(), data.end() - 6), 5, 8, false
    );

    return std::make_pair(hrp, decoded_data);
}

} // namespace chrono_address