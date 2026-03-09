#include "test_framework.hpp"
#include "crypto/kyber_crypto.hpp"
#include "crypto/aes_crypto.hpp"
#include "util/bytes.hpp"
#include "util/log.hpp"
#include <iostream>
#include <vector>

using namespace chrono_crypto;
using namespace chrono_util;

TEST_CASE(KyberCryptoBasicFlow, "KyberCrypto Basic Flow") {
    ASSERT_TRUE(KyberCrypto::init(), "KyberCrypto init should succeed");

    // 1. Generate KeyPair
    auto kp = KyberCrypto::generate_keypair();
    ASSERT_TRUE(kp.has_value(), "KeyPair generation should succeed");
    ASSERT_EQ(kp->public_key.size(), KyberCrypto::get_public_key_size(), "Public key size mismatch");
    ASSERT_EQ(kp->private_key.size(), KyberCrypto::get_private_key_size(), "Private key size mismatch");

    // 2. Encapsulate
    auto enc = KyberCrypto::encapsulate(kp->public_key);
    ASSERT_TRUE(enc.has_value(), "Encapsulation should succeed");
    ASSERT_EQ(enc->ciphertext.size(), KyberCrypto::get_ciphertext_size(), "Ciphertext size mismatch");
    ASSERT_EQ(enc->shared_secret.size(), KyberCrypto::get_shared_secret_size(), "Shared secret size mismatch");

    // 3. Decapsulate
    auto shared_secret = KyberCrypto::decapsulate(enc->ciphertext, kp->private_key);
    ASSERT_TRUE(shared_secret.has_value(), "Decapsulation should succeed");
    ASSERT_EQ(shared_secret->size(), KyberCrypto::get_shared_secret_size(), "Decapsulated secret size mismatch");

    // 4. Verify Shared Secrets match
    ASSERT_TRUE(*shared_secret == enc->shared_secret, "Shared secrets should match");

    KyberCrypto::cleanup();
}

TEST_CASE(AESCryptoBasicFlow, "AESCrypto Basic Flow") {
    Bytes key(32, 0xAB); // 256-bit key
    Bytes iv(12, 0xCD);  // 96-bit IV
    Bytes plaintext = string_to_bytes("Hello, Quantum World!");

    // 1. Encrypt
    auto encrypted = AESCrypto::encrypt(key, iv, plaintext);
    ASSERT_TRUE(encrypted.has_value(), "Encryption should succeed");
    
    // Ciphertext size should be plaintext size + tag size (16)
    ASSERT_EQ(encrypted->size(), plaintext.size() + 16, "Ciphertext size mismatch");

    // 2. Decrypt
    // Split tag
    size_t tag_size = 16;
    size_t ct_size = encrypted->size() - tag_size;
    Bytes ciphertext(encrypted->begin(), encrypted->begin() + ct_size);
    Bytes tag(encrypted->begin() + ct_size, encrypted->end());

    auto decrypted = AESCrypto::decrypt(key, iv, ciphertext, tag);
    ASSERT_TRUE(decrypted.has_value(), "Decryption should succeed");
    ASSERT_TRUE(*decrypted == plaintext, "Decrypted text should match plaintext");
}

TEST_CASE(AESCryptoTamperCheck, "AESCrypto Tamper Check") {
    Bytes key(32, 0xAB);
    Bytes iv(12, 0xCD);
    Bytes plaintext = string_to_bytes("Secret Data");

    auto encrypted = AESCrypto::encrypt(key, iv, plaintext);
    ASSERT_TRUE(encrypted.has_value(), "Encryption should succeed");

    // Tamper with ciphertext
    (*encrypted)[0] ^= 0xFF;

    size_t tag_size = 16;
    size_t ct_size = encrypted->size() - tag_size;
    Bytes ciphertext(encrypted->begin(), encrypted->begin() + ct_size);
    Bytes tag(encrypted->begin() + ct_size, encrypted->end());

    auto decrypted = AESCrypto::decrypt(key, iv, ciphertext, tag);
    ASSERT_FALSE(decrypted.has_value(), "Decryption should fail for tampered data");
}
