//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file signer.hpp
 * @brief This file defines the ISigner interface, an abstract base class for cryptographic signing mechanisms.
 *
 * The ISigner interface establishes a contract for any class that provides digital signing
 * and verification functionalities. It ensures a consistent API for different cryptographic
 * algorithms (e.g., Dilithium, HMAC) that might be used for signing messages within the Chronos project.
 *
 * Key functionalities defined by this interface:
 * - `get_public_key()`: Retrieves the public key associated with the signer.
 * - `sign(const Bytes& message)`: Generates a digital signature for a given message.
 * - `verify(const Bytes& public_key, const Bytes& message, const Bytes& signature)`: Verifies a digital signature.
 */

#pragma once

#include "util/bytes.hpp"
#include <string>

using chrono_util::Bytes;

namespace chrono_crypto {

class ISigner {
public:
    virtual ~ISigner() = default;

    virtual Bytes get_public_key() const = 0;
    virtual std::string get_address() const = 0;
    virtual Bytes sign(const Bytes& message) const = 0;
    virtual Bytes sign_message(const Bytes& message_bytes) const = 0; // NEW: Take Bytes, return Bytes
    virtual bool verify(const Bytes& public_key, const Bytes& message, const Bytes& signature) const = 0;
};

} // namespace chrono_crypto