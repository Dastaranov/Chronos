#include "crypto/aes_crypto.hpp"
#include "util/log.hpp"
#include <openssl/evp.h>
#include <openssl/err.h>
#include <cstring>

namespace chrono_crypto {

std::optional<chrono_util::Bytes> AESCrypto::encrypt(const chrono_util::Bytes& key, const chrono_util::Bytes& iv, const chrono_util::Bytes& plaintext) {
    if (key.size() != KEY_SIZE || iv.size() != IV_SIZE) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Invalid key or IV size for AES encryption");
        return std::nullopt;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::nullopt;

    chrono_util::Bytes ciphertext(plaintext.size());
    chrono_util::Bytes tag(TAG_SIZE);
    int len;
    int ciphertext_len;

    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), plaintext.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    ciphertext_len = len;

    if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len)) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    ciphertext_len += len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE, tag.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    EVP_CIPHER_CTX_free(ctx);

    // Append tag to ciphertext
    ciphertext.insert(ciphertext.end(), tag.begin(), tag.end());
    return ciphertext;
}

std::optional<chrono_util::Bytes> AESCrypto::decrypt(const chrono_util::Bytes& key, const chrono_util::Bytes& iv, const chrono_util::Bytes& ciphertext, const chrono_util::Bytes& tag) {
    if (key.size() != KEY_SIZE || iv.size() != IV_SIZE || tag.size() != TAG_SIZE) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Invalid key, IV, or tag size for AES decryption");
        return std::nullopt;
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) return std::nullopt;

    chrono_util::Bytes plaintext(ciphertext.size());
    int len;
    int plaintext_len;

    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_SIZE, NULL)) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, key.data(), iv.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }
    plaintext_len = len;

    if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_SIZE, (void*)tag.data())) {
        EVP_CIPHER_CTX_free(ctx);
        return std::nullopt;
    }

    int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);

    EVP_CIPHER_CTX_free(ctx);

    if (ret > 0) {
        plaintext_len += len;
        plaintext.resize(plaintext_len); // Resize to actual length
        return plaintext;
    } else {
        return std::nullopt;
    }
}

} // namespace chrono_crypto
