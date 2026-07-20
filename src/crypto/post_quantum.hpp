//
// Created by Chronos | 2026 | Belgium
//

/**
 * @file post_quantum.hpp
 * @brief Canonical FIPS naming aliases for post-quantum primitives used in Chronos.
 */

#pragma once

#include "crypto/signer_dilithium.hpp"
#include "crypto/kyber_crypto.hpp"

namespace chrono_crypto {

using ML_DSA_Signer = SignerMLDSA;
using ML_KEM_Crypto = MLKEMCrypto;

} // namespace chrono_crypto

