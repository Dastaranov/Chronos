//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file blake3.hpp
 * @brief This file provides an interface for the BLAKE3 cryptographic hash function.
 *
 * BLAKE3 is a fast, secure, and highly parallelizable cryptographic hash function.
 * This header defines functions for computing standard BLAKE3 hashes and keyed BLAKE3 hashes,
 * which are useful for message authentication codes (MACs) or key derivation.
 *
 * Key functionalities include:
 * - `blake3_hash`: Computes the 32-byte (256-bit) BLAKE3 hash of a given data buffer.
 * - `blake3`: An inline helper function to compute the BLAKE3 hash for a `std::vector<uint8_t>`.
 * - `blake3_keyed_hash`: Computes a keyed BLAKE3 hash using a secret key and a data buffer.
 * - `blake3_keyed`: An inline helper function to compute the keyed BLAKE3 hash for `std::vector<uint8_t>`.
 */

#pragma once
#include <cstdint>
#include <vector>

namespace chrono_crypto {

  /**
   * @brief Computes the 32-byte BLAKE3 hash of a given data buffer.
   *
   * This function takes a pointer to a data buffer and its length, and computes
   * the 256-bit (32-byte) BLAKE3 hash, storing the result in the provided output array.
   *
   * @param data A pointer to the input data buffer.
   * @param len The length of the input data in bytes.
   * @param out32 A pointer to a 32-byte array where the computed hash will be stored.
   */
  void blake3_hash(const uint8_t* data, size_t len, uint8_t out32[32]);

  /**
   * @brief Computes the 32-byte BLAKE3 hash of a `std::vector<uint8_t>`.
   *
   * This is an inline helper function that wraps `blake3_hash` for convenience when
   * working with `std::vector<uint8_t>` as input. It returns the hash as a new `std::vector<uint8_t>`.
   *
   * @param v The input data as a `std::vector<uint8_t>`.
   * @return A `std::vector<uint8_t>` containing the 32-byte BLAKE3 hash.
   */
  inline std::vector<uint8_t> blake3(const std::vector<uint8_t>& v){
    uint8_t out[32]; ///< @var out A temporary 32-byte array to store the hash result.
    blake3_hash(v.data(), v.size(), out);
    return {out, out+32};
  }

  /**
   * @brief Computes a keyed BLAKE3 hash of a given data buffer.
   *
   * This function computes a 256-bit (32-byte) BLAKE3 hash using a secret key.
   * Keyed hashing is useful for message authentication or key derivation.
   *
   * @param key A pointer to the secret key buffer.
   * @param key_len The length of the secret key in bytes.
   * @param data A pointer to the input data buffer.
   * @param data_len The length of the input data in bytes.
   * @param out32 A pointer to a 32-byte array where the computed keyed hash will be stored.
   */
  void blake3_keyed_hash(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out32[32]);

  /**
   * @brief Computes a keyed BLAKE3 hash for `std::vector<uint8_t>` inputs.
   *
   * This is an inline helper function that wraps `blake3_keyed_hash` for convenience when
   * working with `std::vector<uint8_t>` for both the key and data inputs. It returns the
   * keyed hash as a new `std::vector<uint8_t>`.
   *
   * @param key The secret key as a `std::vector<uint8_t>`.
   * @param data The input data as a `std::vector<uint8_t>`.
   * @return A `std::vector<uint8_t>` containing the 32-byte keyed BLAKE3 hash.
   */
  inline std::vector<uint8_t> blake3_keyed(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data){
    uint8_t out[32]; ///< @var out A temporary 32-byte array to store the keyed hash result.
    blake3_keyed_hash(key.data(), key.size(), data.data(), data.size(), out);
    return {out, out+32};
  }

} // namespace chrono_crypto
