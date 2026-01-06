#include "crypto/kyber_crypto.hpp"
#include "util/log.hpp"

#ifdef CHRONOS_USE_OQS
#include <oqs/oqs.h>
#endif

#include <cstring>

namespace chrono_crypto {

#ifdef CHRONOS_USE_OQS
static const char* OQS_ALG = OQS_KEM_alg_ml_kem_512; // Standardized ML-KEM-512 (Kyber)
#endif

bool KyberCrypto::init() {
#ifdef CHRONOS_USE_OQS
    OQS_init();
    return true;
#else
    LOG_WARN(chrono_util::LogCategory::CRYPTO, "LibOQS not enabled. KyberCrypto disabled.");
    return false;
#endif
}

void KyberCrypto::cleanup() {
#ifdef CHRONOS_USE_OQS
    OQS_destroy();
#endif
}

std::optional<KyberCrypto::KeyPair> KyberCrypto::generate_keypair() {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    if (!kem) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to create OQS_KEM object");
        return std::nullopt;
    }

    KeyPair kp;
    kp.public_key.resize(kem->length_public_key);
    kp.private_key.resize(kem->length_secret_key);

    if (OQS_KEM_keypair(kem, kp.public_key.data(), kp.private_key.data()) != OQS_SUCCESS) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "OQS_KEM_keypair failed");
        OQS_KEM_free(kem);
        return std::nullopt;
    }

    OQS_KEM_free(kem);
    return kp;
#else
    return std::nullopt;
#endif
}

std::optional<KyberCrypto::Encapsulation> KyberCrypto::encapsulate(const chrono_util::Bytes& public_key) {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    if (!kem) {
        return std::nullopt;
    }

    if (public_key.size() != kem->length_public_key) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Invalid public key size");
        OQS_KEM_free(kem);
        return std::nullopt;
    }

    Encapsulation enc;
    enc.ciphertext.resize(kem->length_ciphertext);
    enc.shared_secret.resize(kem->length_shared_secret);

    if (OQS_KEM_encaps(kem, enc.ciphertext.data(), enc.shared_secret.data(), public_key.data()) != OQS_SUCCESS) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "OQS_KEM_encaps failed");
        OQS_KEM_free(kem);
        return std::nullopt;
    }

    OQS_KEM_free(kem);
    return enc;
#else
    return std::nullopt;
#endif
}

std::optional<chrono_util::Bytes> KyberCrypto::decapsulate(const chrono_util::Bytes& ciphertext, const chrono_util::Bytes& private_key) {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    if (!kem) {
        return std::nullopt;
    }

    if (ciphertext.size() != kem->length_ciphertext || private_key.size() != kem->length_secret_key) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Invalid ciphertext or private key size");
        OQS_KEM_free(kem);
        return std::nullopt;
    }

    chrono_util::Bytes shared_secret(kem->length_shared_secret);

    if (OQS_KEM_decaps(kem, shared_secret.data(), ciphertext.data(), private_key.data()) != OQS_SUCCESS) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "OQS_KEM_decaps failed");
        OQS_KEM_free(kem);
        return std::nullopt;
    }

    OQS_KEM_free(kem);
    return shared_secret;
#else
    return std::nullopt;
#endif
}

size_t KyberCrypto::get_public_key_size() {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    size_t len = kem ? kem->length_public_key : 0;
    OQS_KEM_free(kem);
    return len;
#else
    return 0;
#endif
}

size_t KyberCrypto::get_private_key_size() {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    size_t len = kem ? kem->length_secret_key : 0;
    OQS_KEM_free(kem);
    return len;
#else
    return 0;
#endif
}

size_t KyberCrypto::get_ciphertext_size() {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    size_t len = kem ? kem->length_ciphertext : 0;
    OQS_KEM_free(kem);
    return len;
#else
    return 0;
#endif
}

size_t KyberCrypto::get_shared_secret_size() {
#ifdef CHRONOS_USE_OQS
    OQS_KEM* kem = OQS_KEM_new(OQS_ALG);
    size_t len = kem ? kem->length_shared_secret : 0;
    OQS_KEM_free(kem);
    return len;
#else
    return 0;
#endif
}

} // namespace chrono_crypto
