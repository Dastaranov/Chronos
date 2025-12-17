//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file signer_hmac.hpp
 * @brief This file defines the SignerHMAC class, which implements the ISigner interface using HMAC-BLAKE3 for message signing.
 *
 * The SignerHMAC class provides a concrete implementation of a digital signer based on
 * HMAC (Hash-based Message Authentication Code) using BLAKE3 as the underlying hash function.
 * This approach provides message authentication and integrity verification, but not non-repudiation
 * as it uses a shared secret key.
 *
 * Key functionalities include:
 * - `SignerHMAC(const Bytes& key)`: Constructor to initialize the signer with a raw byte key.
 * - `SignerHMAC(const std::string& key_string)`: Constructor to initialize the signer with a key provided as a string.
 * - `get_public_key()`: Returns the "public key" (which is the HMAC key itself, or a derivative, depending on usage).
 * - `sign(const Bytes& message)`: Computes an HMAC-BLAKE3 signature for a given message.
 * - `verify(const Bytes& public_key, const Bytes& message, const Bytes& signature)`: Verifies an HMAC-BLAKE3 signature.
 */

#pragma once

#include "crypto/signer.hpp"
#include <vector>
#include <string>
#include "util/bytes.hpp" // For Bytes type alias
using chrono_util::Bytes;

namespace chrono_crypto {

/**
 * @class SignerHMAC
 * @brief Implements the ISigner interface using HMAC-BLAKE3 for message signing and verification.
 *
 * This class provides a symmetric key-based signing mechanism. Unlike asymmetric cryptography
 * (like Dilithium), HMAC uses a shared secret key for both signing and verification.
 * It's suitable for scenarios where both parties share a secret and need to ensure message
 * integrity and authenticity.
 */
class SignerHMAC : public ISigner {
public:
    /**
     * @brief Constructs a SignerHMAC object with a given key in byte format.
     *
     * Initializes the HMAC signer with a secret key provided as a `Bytes` object.
     * This key will be used for both signing messages and verifying signatures.
     *
     * @param key The secret key for HMAC, represented as a `Bytes` object.
     */
    explicit SignerHMAC(const Bytes& key);

    /**
     * @brief Constructs a SignerHMAC object with a given key in string format.
     *
     * Initializes the HMAC signer with a secret key provided as a `std::string`.
     * The string is converted to a `Bytes` object internally.
     *
     * @param key_string The secret key for HMAC, represented as a `std::string`.
     */
    explicit SignerHMAC(const std::string& key_string);

    /**
     * @brief Retrieves the "public key" associated with this HMAC signer.
     *
     * In the context of HMAC, there isn't a traditional public key. This method
     * typically returns the HMAC key itself, or a derivative, depending on how
     * it's intended to be used for verification. For HMAC, the "public key"
     * is effectively the shared secret key.
     *
     * @return A `Bytes` object containing the HMAC key.
     */
    Bytes get_public_key() const override;

    /**
     * @brief Computes an HMAC-BLAKE3 signature for a given message.
     *
     * This method uses the internal secret key to compute an HMAC-BLAKE3 tag
     * for the provided message. This tag serves as the signature.
     *
     * @param message The message to be signed, represented as a `Bytes` object.
     * @return A `Bytes` object containing the computed HMAC-BLAKE3 signature.
     */
    Bytes sign(const Bytes& message) const override;

    /**
     * @brief Verifies an HMAC-BLAKE3 signature against a message and a public key.
     *
     * This method verifies if the provided signature is valid for the given message
     * and public key (which should be the shared secret HMAC key). It recomputes
     * the HMAC-BLAKE3 tag and compares it with the provided signature.
     *
     * @param public_key The public key (shared secret HMAC key) used for verification, as a `Bytes` object.
     * @param message The original message that was signed, as a `Bytes` object.
     * @param signature The HMAC-BLAKE3 signature to be verified, as a `Bytes` object.
     * @return `true` if the signature is valid, `false` otherwise.
     */
    bool verify(const Bytes& public_key, const Bytes& message, const Bytes& signature) const override;

    std::string get_address() const override; // NEW
    Bytes sign_message(const Bytes& message_bytes) const override; // NEW

private:
    Bytes key_; ///< @var key_ The secret key used for HMAC operations, stored as a `Bytes` object.
};

} // namespace chrono_crypto