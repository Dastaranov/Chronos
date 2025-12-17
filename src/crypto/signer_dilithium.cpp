//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file signer_dilithium.cpp
 * @brief This file implements the SignerDilithium class, providing Dilithium post-quantum digital signature functionalities.
 *
 * This implementation leverages the Open Quantum Safe (OQS) library to perform Dilithium-3
 * key generation, message signing, and signature verification. It ensures secure handling
 * of cryptographic keys and provides robust error checking for all operations.
 *
 * Key functions implemented:
 * - `SignerDilithium::SignerDilithium()`: Generates a new Dilithium-3 key pair.
 * - `SignerDilithium::~SignerDilithium()`: Frees allocated cryptographic resources.
 * - `SignerDilithium::get_public_key()`: Returns the public key.
 * - `SignerDilithium::sign()`: Signs a message using the private key.
 * - `SignerDilithium::verify_static()`: Verifies a signature using a public key.
 */

#include "crypto/signer_dilithium.hpp"
#include "util/log.hpp"
#include <stdexcept>
#include <cstring> // For std::memcpy
#include "address/address.hpp"

namespace chrono_crypto {

bool SignerDilithium::generate_key_pair(Bytes& public_key, Bytes& private_key) {
    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65);
    if (sig == nullptr) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to create OQS_SIG for key generation");
        return false;
    }

    public_key.resize(sig->length_public_key);
    private_key.resize(sig->length_secret_key);

    bool success = (OQS_SIG_keypair(sig, public_key.data(), private_key.data()) == OQS_SUCCESS);

    OQS_SIG_free(sig);
    return success;
}

SignerDilithium::SignerDilithium() {
    sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65);
    if (sig == nullptr) {
        throw std::runtime_error("Failed to create OQS_SIG for ML-DSA-65");
    }

    Bytes full_private_key;
    if (!generate_key_pair(public_key, full_private_key)) {
        OQS_SIG_free(sig);
        throw std::runtime_error("Failed to generate Dilithium keypair in constructor");
    }

    private_key = new uint8_t[sig->length_secret_key];
    std::memcpy(private_key, full_private_key.data(), sig->length_secret_key);
}

SignerDilithium::SignerDilithium(const Bytes& private_key_bytes) {
    sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65);
    if (sig == nullptr) {
        throw std::runtime_error("Failed to create OQS_SIG for ML-DSA-65");
    }

    if (private_key_bytes.size() != sig->length_secret_key) {
        OQS_SIG_free(sig);
        throw std::invalid_argument("Invalid private key size for Dilithium-3");
    }

    // Allocate and copy the full private key
    private_key = new uint8_t[sig->length_secret_key];
    std::memcpy(private_key, private_key_bytes.data(), sig->length_secret_key);

    // Extract the public key from the end of the private key data
    public_key.resize(sig->length_public_key);
    const uint8_t* pk_start_ptr = private_key + (sig->length_secret_key - sig->length_public_key);
    std::memcpy(public_key.data(), pk_start_ptr, sig->length_public_key);
}


/**
 * @brief Destroys the SignerDilithium object and securely frees cryptographic resources.
 *
 * This destructor is responsible for releasing the OQS_SIG context and securely deleting
 * the private key array to prevent memory leaks and ensure that sensitive key material
 * is not left in memory.
 */
SignerDilithium::~SignerDilithium() {
    if (sig) {
        OQS_SIG_free(sig);
    }
    // Securely clear and delete the private key
    if (private_key) {
        OQS_MEM_cleanse(private_key, sig->length_secret_key);
        delete[] private_key;
        private_key = nullptr; // Prevent double free
    }
}

/**
 * @brief Retrieves the public key of this signer.
 *
 * @return A `Bytes` object containing the public key. This key can be safely shared
 *         for signature verification.
 */
Bytes SignerDilithium::get_public_key() const {
    return public_key;
}

/**
 * @brief Signs a given message using the Dilithium-3 private key.
 *
 * This method takes a message and computes its digital signature using the private key
 * associated with this `SignerDilithium` instance. The signature is generated using
 * the OQS_SIG_sign function.
 *
 * @param message The message to be signed, represented as a `Bytes` object.
 * @return A `Bytes` object containing the generated Dilithium-3 signature.
 * @throw std::runtime_error if the signing operation fails.
 */
Bytes SignerDilithium::sign(const Bytes& message) const {
    Bytes signature;
    signature.resize(sig->length_signature);
    size_t signature_len;

    if (OQS_SIG_sign(sig, signature.data(), &signature_len, message.data(), message.size(), private_key) != OQS_SUCCESS) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to sign message with Dilithium-3");
        throw std::runtime_error("Failed to sign message with Dilithium-3");
    }
    signature.resize(signature_len); // Adjust size to actual signature length
    return signature;
}

/**
 * @brief Statically verifies a Dilithium-3 signature.
 *
 * This static method verifies if a given signature is valid for a specific message and public key.
 * It creates a temporary OQS_SIG context for verification, performs the check using OQS_SIG_verify,
 * and then frees the temporary context. This approach allows verification without needing an
 * existing `SignerDilithium` instance.
 *
 * @param public_key The public key used for verification, as a `Bytes` object.
 * @param message The original message that was signed, as a `Bytes` object.
 * @param signature The Dilithium-3 signature to be verified, as a `Bytes` object.
 * @return `true` if the signature is valid, `false` otherwise.
 */
bool SignerDilithium::verify_static(const Bytes& public_key, const Bytes& message, const Bytes& signature) {
    OQS_SIG* sig = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65); // Use correct algorithm name
    if (sig == nullptr) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to create OQS_SIG for ML-DSA-65 verification");
        return false;
    }

    bool is_valid = (OQS_SIG_verify(sig, message.data(), message.size(), signature.data(), signature.size(), public_key.data()) == OQS_SUCCESS);
    
    OQS_SIG_free(sig);
    return is_valid;
}

std::string SignerDilithium::get_address() const {
    return chrono_address::Address(get_public_key()).to_string();
}

Bytes SignerDilithium::sign_message(const Bytes& message_bytes) const {
    return sign(message_bytes);
}

} // namespace chrono_crypto