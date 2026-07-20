#pragma once

#include "util/bytes.hpp"
#include <vector>
#include <string>
#include <optional>

namespace chrono_crypto {

class MLKEMCrypto {
public:
    // Using ML-KEM-512 for balance between security and performance/size.
    
    struct KeyPair {
        chrono_util::Bytes public_key;
        chrono_util::Bytes private_key;
    };

    struct Encapsulation {
        chrono_util::Bytes ciphertext;
        chrono_util::Bytes shared_secret;
    };

    static bool init();
    static void cleanup();

    static std::optional<KeyPair> generate_keypair();
    static std::optional<Encapsulation> encapsulate(const chrono_util::Bytes& public_key);
    static std::optional<chrono_util::Bytes> decapsulate(const chrono_util::Bytes& ciphertext, const chrono_util::Bytes& private_key);

    // Constants for buffer sizing (will be populated after init or hardcoded based on known Kyber512 specs)
    static size_t get_public_key_size();
    static size_t get_private_key_size();
    static size_t get_ciphertext_size();
    static size_t get_shared_secret_size();
};

using KyberCrypto = MLKEMCrypto;

} // namespace chrono_crypto
