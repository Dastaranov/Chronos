//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file signer_dilithium.hpp
 * @brief This file defines the SignerMLDSA class, implementing the ISigner interface using ML-DSA (FIPS 204).
 *
 * The SignerMLDSA class provides functionalities for generating ML-DSA key pairs,
 * signing messages, and verifying signatures. It leverages the Open Quantum Safe (OQS) library
 * for the underlying cryptographic operations. This class is designed to be a concrete
 * implementation of a digital signer, adhering to the `ISigner` interface.
 *
 * Key functionalities include:
 * - `SignerMLDSA()`: Constructor to initialize the ML-DSA signer, generating a new key pair.
 * - `~SignerMLDSA()`: Destructor to securely clear and free cryptographic resources.
 * - `get_public_key()`: Retrieves the public key associated with this signer.
 * - `sign(const Bytes& message)`: Signs a given message using the Dilithium private key.
 * - `verify_static(const Bytes& public_key, const Bytes& message, const Bytes& signature)`: A static method to verify a Dilithium signature against a message and public key.
 * - `verify(const Bytes& public_key, const Bytes& message, const Bytes& signature)`: Overrides the `ISigner` interface's verify method, delegating to the static verification.
 */

#pragma once

#include "crypto/signer.hpp"
#include "util/bytes.hpp"
using chrono_util::Bytes;

#ifdef CHRONOS_USE_OQS
#include <oqs/oqs.h>
#endif

namespace chrono_crypto {

/**
 * @class SignerMLDSA
 * @brief Implements the ISigner interface using the ML-DSA post-quantum signature scheme.
 *
 * This class provides a concrete implementation for digital signatures based on the Dilithium
 * algorithm, which is designed to be resistant to attacks from quantum computers. It manages
 * the generation, storage, and usage of Dilithium key pairs for signing and verification.
 * The class ensures proper resource management for cryptographic keys.
 */
class SignerMLDSA : public ISigner {
public:
    /**
     * @brief Constructs a SignerMLDSA object and generates a new ML-DSA key pair.
     *
     * Upon construction, a new public and private key pair for the Dilithium signature scheme
     * is generated. These keys are then used for subsequent signing and verification operations.
     * This constructor relies on the OQS library for key generation.
     */
    SignerMLDSA();

    /**
     * @brief Constructs a SignerMLDSA object from an existing key pair.
     *
     * @param public_key_bytes The public key to use for this signer.
     * @param private_key_bytes The private key to use for this signer.
     */
    SignerMLDSA(const Bytes& public_key_bytes, const Bytes& private_key_bytes);

    /**
     * @brief Constructs a SignerMLDSA object from an existing private key.
     * @deprecated This constructor is unsafe as it cannot reliably recover the public key from the private key with liboqs.
     *             Use the constructor taking both keys instead.
     *
     * @param private_key_bytes The private key to use for this signer.
     */
    explicit SignerMLDSA(const Bytes& private_key_bytes);


    /**
     * @brief Destroys the SignerMLDSA object and securely frees cryptographic resources.
     *
     * The destructor is responsible for securely clearing the private key from memory
     * and freeing any allocated resources associated with the OQS library to prevent
     * sensitive information leakage.
     */
    ~SignerMLDSA();

    // Deleted copy and move constructors/assignments to prevent unintended copying
    // of cryptographic keys and ensure secure handling of resources.
    SignerMLDSA(const SignerMLDSA&) = delete;
    SignerMLDSA& operator=(const SignerMLDSA&) = delete;
    SignerMLDSA(SignerMLDSA&&) = delete;
    SignerMLDSA& operator=(SignerMLDSA&&) = delete;

    /**
     * @brief Retrieves the public key of this signer.
     *
     * This method returns the public key that was generated during the construction of this
     * `SignerMLDSA` instance. The public key can be shared to allow others to verify
     * signatures created by this signer.
     *
     * @return A `Bytes` object containing the public key.
     */
    Bytes get_public_key() const override;

    /**
     * @brief Signs a given message using the Dilithium private key.
     *
     * This method takes a message as input and generates a digital signature using the
     * private key held by this `SignerMLDSA` instance. The signature can then be
     * verified by anyone with the corresponding public key.
     *
     * @param message The message to be signed, represented as a `Bytes` object.
     * @return A `Bytes` object containing the generated ML-DSA signature.
     */
    Bytes sign(const Bytes& message) const override;
    
    /**
     * @brief Statically verifies an ML-DSA signature.
     *
     * This static method verifies if a given signature is valid for a specific message
     * and public key. It does not depend on the state of a `SignerDilithium` instance,
     * making it suitable for general signature verification.
     *
     * @param public_key The public key used to verify the signature, as a `Bytes` object.
     * @param message The original message that was signed, as a `Bytes` object.
     * @param signature The Dilithium signature to be verified, as a `Bytes` object.
     * @return `true` if the signature is valid, `false` otherwise.
     */
    static bool verify_static(const Bytes& public_key, const Bytes& message, const Bytes& signature);

    /**
     * @brief Statically generates a new ML-DSA key pair.
     *
     * @param[out] public_key The generated public key.
     * @param[out] private_key The generated private key.
     * @return `true` if key generation was successful, `false` otherwise.
     */
    static bool generate_key_pair(Bytes& public_key, Bytes& private_key);

    /**
     * @brief Verifies an ML-DSA signature using the `ISigner` interface.
     *
     * This method overrides the virtual `verify` function from the `ISigner` interface.
     * It delegates the actual verification logic to the `verify_static` method, providing
     * a consistent interface for signature verification.
     *
     * @param public_key The public key used to verify the signature, as a `Bytes` object.
     * @param message The original message that was signed, as a `Bytes` object.
     * @param signature The ML-DSA signature to be verified, as a `Bytes` object.
     * @return `true` if the signature is valid, `false` otherwise.
     */
    bool verify(const Bytes& public_key, const Bytes& message, const Bytes& signature) const override {
        return verify_static(public_key, message, signature);
    }

    std::string get_address() const override;
    Bytes sign_message(const Bytes& message_bytes) const override;

private:
#ifdef CHRONOS_USE_OQS
    OQS_SIG* sig = nullptr; ///< @var sig A pointer to the OQS_SIG object, managing the ML-DSA algorithm context.
    Bytes public_key_; ///< @var public_key_ The public key of this signer, stored as a Bytes object.
    uint8_t* private_key_ = nullptr; ///< @var private_key_ A pointer to the raw private key bytes, managed by OQS.
#endif
};

using SignerDilithium = SignerMLDSA;

} // namespace chrono_crypto
