//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file blake3.cpp
 * @brief This file implements the BLAKE3 cryptographic hash functions using the external BLAKE3 library.
 *
 * This file provides the concrete implementations for computing standard and keyed BLAKE3 hashes.
 * It acts as a wrapper around the `external/blake3` C library, making its functionalities
 * available within the `chrono_crypto` namespace.
 *
 * Key functions implemented:
 * - `blake3_hash`: Computes a standard 32-byte BLAKE3 hash.
 * - `blake3_keyed_hash`: Computes a 32-byte BLAKE3 hash using a provided secret key.
 */

#include "crypto/blake3.hpp"
extern "C" {
#include "external/blake3/blake3.h"
}

namespace chrono_crypto {

/**
 * @brief Computes the 32-byte BLAKE3 hash of a given data buffer.
 *
 * This function initializes a BLAKE3 hasher, updates it with the provided data,
 * and then finalizes the hash computation, storing the 32-byte result in `out32`.
 *
 * @param data A pointer to the input data buffer.
 * @param len The length of the input data in bytes.
 * @param out32 A pointer to a 32-byte array where the computed hash will be stored.
 */
void blake3_hash(const uint8_t* data, size_t len, uint8_t out32[32]) {
    blake3_hasher hasher; ///< @var hasher An instance of the BLAKE3 hasher context.
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data, len);
    blake3_hasher_finalize(&hasher, out32, 32);
}

/**
 * @brief Computes a keyed BLAKE3 hash of a given data buffer.
 *
 * This function initializes a BLAKE3 hasher with a secret key, updates it with the provided data,
 * and then finalizes the hash computation, storing the 32-byte result in `out32`.
 * Keyed hashing provides a way to authenticate messages or derive keys.
 *
 * @param key A pointer to the secret key buffer.
 * @param key_len The length of the secret key in bytes.
 * @param data A pointer to the input data buffer.
 * @param data_len The length of the input data in bytes.
 * @param out32 A pointer to a 32-byte array where the computed keyed hash will be stored.
 */
void blake3_keyed_hash(const uint8_t* key, size_t key_len, const uint8_t* data, size_t data_len, uint8_t out32[32]) {
    blake3_hasher hasher; ///< @var hasher An instance of the BLAKE3 hasher context.
    blake3_hasher_init_keyed(&hasher, key);
    blake3_hasher_update(&hasher, data, data_len);
    blake3_hasher_finalize(&hasher, out32, 32);
}

} // namespace chrono_crypto