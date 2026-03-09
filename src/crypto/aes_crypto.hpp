#pragma once

#include "util/bytes.hpp"
#include <optional>
#include <vector>

namespace chrono_crypto {

class AESCrypto {
public:
    static constexpr size_t KEY_SIZE = 32; // AES-256
    static constexpr size_t IV_SIZE = 12;  // GCM standard IV size
    static constexpr size_t TAG_SIZE = 16; // GCM standard tag size

    /**
     * @brief Encrypts plaintext using AES-256-GCM.
     * @param key 32-byte key
     * @param iv 12-byte IV
     * @param plaintext Data to encrypt
     * @return Ciphertext + Tag appended, or nullopt on failure
     */
    static std::optional<chrono_util::Bytes> encrypt(const chrono_util::Bytes& key, const chrono_util::Bytes& iv, const chrono_util::Bytes& plaintext);

    /**
     * @brief Decrypts ciphertext using AES-256-GCM.
     * @param key 32-byte key
     * @param iv 12-byte IV
     * @param ciphertext Ciphertext (without tag)
     * @param tag 16-byte authentication tag
     * @return Plaintext, or nullopt on failure
     */
    static std::optional<chrono_util::Bytes> decrypt(const chrono_util::Bytes& key, const chrono_util::Bytes& iv, const chrono_util::Bytes& ciphertext, const chrono_util::Bytes& tag);
};

} // namespace chrono_crypto
