//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file signer_hmac.cpp
 * @brief This file implements the SignerHMAC class, providing HMAC-BLAKE3 based message signing and verification.
 *
 * This implementation uses the BLAKE3 hash function in HMAC mode to provide message authentication.
 * It allows for the creation of `SignerHMAC` instances using either a raw byte vector or a string
 * as the secret key. The core functionalities include signing messages and verifying their integrity
 * and authenticity using the shared secret key.
 *
 * Key functions implemented:
 * - `SignerHMAC::SignerHMAC`: Constructors to initialize the signer with a secret key.
 * - `SignerHMAC::get_public_key`: Returns the secret key (acting as a public key for HMAC).
 * - `SignerHMAC::sign`: Computes the HMAC-BLAKE3 signature for a given message.
 * - `SignerHMAC::verify`: Verifies an HMAC-BLAKE3 signature.
 */

#include "crypto/signer_hmac.hpp"
#include "crypto/blake3.hpp"
#include <stdexcept>
#include "address/address.hpp" // NEW: For chrono_address::Address

namespace chrono_crypto {

/**
 * @brief Constructs a SignerHMAC object with a given key in byte format.
 *
 * Initializes the HMAC signer with a secret key provided as a `Bytes` object.
 * This key will be used for both signing messages and verifying signatures.
 * An empty key is considered invalid.
 *
 * @param key The secret key for HMAC, represented as a `Bytes` object.
 * @throw std::invalid_argument if the provided key is empty.
 */
SignerHMAC::SignerHMAC(const Bytes& key) : key_(key) {
    if (key.empty()) {
        throw std::invalid_argument("HMAC key cannot be empty.");
    }
}

/**
 * @brief Constructs a SignerHMAC object with a given key in string format.
 *
 * Initializes the HMAC signer with a secret key provided as a `std::string`.
 * The string is converted to a `Bytes` object internally. An empty key string is considered invalid.
 *
 * @param key_string The secret key for HMAC, represented as a `std::string`.
 * @throw std::invalid_argument if the provided key string is empty.
 */
SignerHMAC::SignerHMAC(const std::string& key_string) : key_(key_string.begin(), key_string.end()) {
    if (key_string.empty()) {
        throw std::invalid_argument("HMAC key cannot be empty.");
    }
}

/**
 * @brief Retrieves the "public key" associated with this HMAC signer.
 *
 * In the context of HMAC, there isn't a traditional public key. This method
 * returns the HMAC key itself, as it is the shared secret required for verification.
 * This is a simplification for the `ISigner` interface, where `get_public_key`
 * is expected.
 *
 * @return A `Bytes` object containing the HMAC key.
 */
Bytes SignerHMAC::get_public_key() const {
    // For HMAC, the "public key" is just the key itself.
    // In a real scenario, this would be handled differently, but for a developer fallback, this is fine.
    return key_;
}

/**
 * @brief Computes an HMAC-BLAKE3 signature for a given message.
 *
 * This method uses the internal secret key (`key_`) to compute an HMAC-BLAKE3 tag
 * for the provided message. The `blake3_keyed` function from `chrono_crypto` is used
 * for this operation. The resulting tag serves as the signature.
 *
 * @param message The message to be signed, represented as a `Bytes` object.
 * @return A `Bytes` object containing the computed HMAC-BLAKE3 signature.
 */
Bytes SignerHMAC::sign(const Bytes& message) const {
    return blake3_keyed(key_, message);
}

/**
 * @brief Verifies an HMAC-BLAKE3 signature against a message and a public key.
 *
 * This method verifies if the provided signature is valid for the given message
 * and public key. For HMAC, the `public_key` parameter is expected to be the
 * shared secret key. It recomputes the HMAC-BLAKE3 tag using the provided
 * `public_key` and `message`, and then compares this recomputed tag with the
 * provided `signature`.
 *
 * @param public_key The public key (shared secret HMAC key) used for verification, as a `Bytes` object.
 * @param message The original message that was signed, as a `Bytes` object.
 * @param signature The HMAC-BLAKE3 signature to be verified, as a `Bytes` object.
 * @return `true` if the recomputed HMAC tag matches the provided signature, `false` otherwise.
 */
bool SignerHMAC::verify(const Bytes& public_key, const Bytes& message, const Bytes& signature) const {
    // For HMAC, the public key is the key.
    return blake3_keyed(public_key, message) == signature;
}

std::string SignerHMAC::get_address() const {
    return chrono_address::Address(get_public_key()).to_string();
}

Bytes SignerHMAC::sign_message(const Bytes& message_bytes) const {
    return sign(message_bytes);
}

} // namespace chrono_crypto
