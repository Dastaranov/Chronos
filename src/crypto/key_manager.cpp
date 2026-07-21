//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file key_manager.cpp
 * @brief Implements the KeyManager class for secure key storage and retrieval.
 *
 * This implementation provides:
 * - Secure file-based key storage
 * - Base58Check encoding/decoding for public keys
 * - Key caching for performance
 * - Directory management and key listing
 */

#include "crypto/key_manager.hpp"
#include "crypto/aes_crypto.hpp"
#include "crypto/blake3.hpp"
#include "util/log.hpp"
#include <fstream>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <openssl/rand.h>
#ifdef CHRONOS_USE_OQS
#include <oqs/oqs.h>
#endif

namespace chrono_crypto {

/**
 * @brief Fills a byte buffer with cryptographically secure random bytes.
 *
 * Uses liboqs RNG when OQS is enabled, otherwise falls back to OpenSSL RAND_bytes.
 *
 * @param buffer Destination buffer to fill.
 * @param length Number of bytes to generate.
 * @return true on success, false on RNG failure.
 */
static bool fill_secure_random(uint8_t* buffer, size_t length) {
#ifdef CHRONOS_USE_OQS
    OQS_randombytes(buffer, length);
    return true;
#else
    return RAND_bytes(buffer, static_cast<int>(length)) == 1;
#endif
}

// Base58Check alphabet (excludes 0, O, I, l to avoid confusion)
const std::string BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

// Helper to derive key from passphrase and salt
static chrono_util::Bytes derive_key(const std::string& passphrase, const chrono_util::Bytes& salt) {
    // Use blake3_keyed(salt, passphrase) as KDF
    // Salt is key, passphrase is data
    // Salt must be 32 bytes for blake3_keyed key
    if (salt.size() != 32) {
        throw std::invalid_argument("Salt must be 32 bytes");
    }
    chrono_util::Bytes passphrase_bytes(passphrase.begin(), passphrase.end());
    return chrono_crypto::blake3_keyed(salt, passphrase_bytes);
}

/**
 * @brief Encodes bytes to Base58 string.
 */
static std::string encode_base58(const std::vector<uint8_t>& data) {
    // Skip & count leading zeros
    int zeros = 0;
    auto it = data.begin();
    while (it != data.end() && *it == 0) {
        zeros++;
        it++;
    }
    
    // Allocate enough space in big-endian base58 representation
    // log(256) / log(58) is approx 1.37
    std::vector<unsigned char> b58((data.end() - it) * 138 / 100 + 1); 
    
    // Process the bytes
    while (it != data.end()) {
        int carry = *it;
        for (auto b58_it = b58.rbegin(); b58_it != b58.rend(); b58_it++) {
            carry += 256 * (*b58_it);
            *b58_it = carry % 58;
            carry /= 58;
        }
        it++;
    }
    
    // Skip leading zeroes in b58 result
    auto b58_it = b58.begin();
    while (b58_it != b58.end() && *b58_it == 0)
        b58_it++;
        
    std::string str;
    str.reserve(zeros + (b58.end() - b58_it));
    str.assign(zeros, '1');
    while (b58_it != b58.end())
        str += BASE58_ALPHABET[*(b58_it++)];
    return str;
}

/**
 * @brief Decodes Base58 string to bytes.
 */
static std::vector<uint8_t> decode_base58(const std::string& str) {
    // Skip and count leading '1's
    int zeros = 0;
    auto it = str.begin();
    while (it != str.end() && *it == '1') {
        zeros++;
        it++;
    }
    
    // Allocate enough space in big-endian base256 representation
    // log(58) / log(256) is approx 0.73
    std::vector<unsigned char> b256((str.end() - it) * 733 / 1000 + 1); 
    
    // Process the characters
    while (it != str.end()) {
        size_t digit = BASE58_ALPHABET.find(*it);
        if (digit == std::string::npos) return {}; // Invalid character
        
        int carry = digit;
        for (auto b256_it = b256.rbegin(); b256_it != b256.rend(); b256_it++) {
            carry += 58 * (*b256_it);
            *b256_it = carry % 256;
            carry /= 256;
        }
        it++;
    }
    
    // Skip leading zeroes in b256
    auto b256_it = b256.begin();
    while (b256_it != b256.end() && *b256_it == 0)
        b256_it++;
        
    std::vector<uint8_t> result;
    result.reserve(zeros + (b256.end() - b256_it));
    result.assign(zeros, 0);
    result.insert(result.end(), b256_it, b256.end());
    return result;
}

/**
 * @brief Encodes bytes to Base58Check format.
 * 
 * Format: Version (var) | Data | Checksum (4 bytes)
 * Checksum = First 4 bytes of BLAKE3(Version | Data)
 */
static std::string encode_base58check(const chrono_util::Bytes& data, const std::string& version_prefix = "cqc") {
    // Note: Standard Base58Check uses a version byte (e.g. 0x00 for Bitcoin).
    // Here we are using a string prefix "cqc" which is Bech32-style, but the user asked for Base58Check.
    // To be strictly Base58Check, we should use a version BYTE.
    // Let's define a version byte for Chronos addresses, e.g., 0x1C (28).
    // However, the existing code expects "cqc" prefix in the string.
    // Let's stick to standard Base58Check logic: [VersionByte][Data][Checksum] -> Base58Encode
    // And maybe prepend "cqc" if needed, or just rely on the version byte to identify it.
    // But wait, the previous code did: return version + "1" + ...
    // Let's implement standard Base58Check with a specific version byte.
    
    uint8_t version_byte = 0x01; // Example version
    
    std::vector<uint8_t> payload;
    payload.push_back(version_byte);
    payload.insert(payload.end(), data.begin(), data.end());
    
    // Calculate checksum (BLAKE3)
    chrono_util::Bytes hash = chrono_crypto::blake3(payload);
    
    // Append first 4 bytes of checksum
    payload.insert(payload.end(), hash.begin(), hash.begin() + 4);
    
    // Encode to Base58
    return encode_base58(payload);
}

/**
 * @brief Decodes Base58Check format back to bytes.
 */
static chrono_util::Bytes decode_base58check(const std::string& encoded) {
    std::vector<uint8_t> decoded = decode_base58(encoded);
    
    // Check length (Version + Data + Checksum(4))
    if (decoded.size() < 5) return {};
    
    // Extract checksum
    std::vector<uint8_t> checksum(decoded.end() - 4, decoded.end());
    
    // Extract payload (Version + Data)
    std::vector<uint8_t> payload(decoded.begin(), decoded.end() - 4);
    
    // Verify checksum
    chrono_util::Bytes hash = chrono_crypto::blake3(payload);
    std::vector<uint8_t> calculated_checksum(hash.begin(), hash.begin() + 4);
    
    if (checksum != calculated_checksum) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Base58Check checksum mismatch");
        return {};
    }
    
    // Check version byte (0x01)
    if (payload[0] != 0x01) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Invalid address version: {}", payload[0]);
        return {};
    }
    
    // Return data (exclude version byte)
    return chrono_util::Bytes(payload.begin() + 1, payload.end());
}

KeyManager::KeyManager(const std::string& key_dir)
    : key_directory_(key_dir) {
    if (!ensure_key_directory()) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to initialize key directory: {}", key_dir);
    } else {
        LOG_INFO(chrono_util::LogCategory::CRYPTO, "KeyManager initialized with key directory: {}", key_dir);
    }
}

bool KeyManager::ensure_key_directory() {
    try {
        if (!std::filesystem::exists(key_directory_)) {
            std::filesystem::create_directories(key_directory_);
            LOG_INFO(chrono_util::LogCategory::CRYPTO, "Created key directory: {}", key_directory_.string());
        }
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to create key directory: {}", e.what());
        return false;
    }
}

std::filesystem::path KeyManager::get_key_path(const std::string& key_id) {
    // Sanitize key_id to prevent path traversal attacks
    std::string safe_id = key_id;
    for (char& c : safe_id) {
        if (c == '/' || c == '\\' || c == ':') {
            c = '_';
        }
    }
    return key_directory_ / (safe_id + ".key");
}

bool KeyManager::save_key_pair(const std::string& key_id, const KeyPair& keys, const std::string& passphrase) {
    // Save private key (encrypted if passphrase provided)
    if (!save_private_key(key_id, keys.private_key, passphrase)) {
        return false;
    }

    // Save public key (plaintext)
    try {
        std::filesystem::path pub_path = get_key_path(key_id);
        pub_path.replace_extension(".pub");
        
        std::ofstream file(pub_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to open public key file for writing: {}", pub_path.string());
            return false;
        }
        
        file.write(reinterpret_cast<const char*>(keys.public_key.data()), keys.public_key.size());
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Error saving public key: {}", e.what());
        return false;
    }
}

std::optional<KeyManager::KeyPair> KeyManager::load_key_pair(const std::string& key_id, const std::string& passphrase) {
    // Load private key
    chrono_util::Bytes private_key = load_private_key(key_id, passphrase);
    if (private_key.empty()) {
        return std::nullopt;
    }

    // Load public key
    try {
        std::filesystem::path pub_path = get_key_path(key_id);
        pub_path.replace_extension(".pub");
        
        if (!std::filesystem::exists(pub_path)) {
            LOG_WARN(chrono_util::LogCategory::CRYPTO, "Public key file not found: {}", pub_path.string());
            // Fallback: Try to extract from private key (deprecated/unsafe but might work for legacy keys if format allows)
            // But we know it fails for liboqs. So we return nullopt or partial?
            // Returning nullopt forces user to regenerate or migrate.
            return std::nullopt;
        }
        
        std::ifstream file(pub_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to open public key file: {}", pub_path.string());
            return std::nullopt;
        }
        
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        chrono_util::Bytes public_key(file_size);
        file.read(reinterpret_cast<char*>(public_key.data()), file_size);
        file.close();
        
        return KeyPair{public_key, private_key};
        
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Error loading public key: {}", e.what());
        return std::nullopt;
    }
}

bool KeyManager::save_private_key(const std::string& key_id, const chrono_util::Bytes& private_key, const std::string& passphrase) {
    try {
        std::filesystem::path key_path = get_key_path(key_id);
        
        // Open file in binary write mode
        std::ofstream file(key_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to open key file for writing: {}", key_path.string());
            return false;
        }
        
        if (passphrase.empty()) {
            // Legacy plaintext save
            file.write(reinterpret_cast<const char*>(private_key.data()), private_key.size());
        } else {
            // Encrypted save
            // Format: MAGIC (4) | SALT (32) | IV (12) | TAG (16) | CIPHERTEXT (variable)
            const char* magic = "CKEY";
            file.write(magic, 4);

            // Generate Salt
            chrono_util::Bytes salt(32);
            if (!fill_secure_random(salt.data(), salt.size())) {
                LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to generate random salt for key: {}", key_id);
                return false;
            }
            file.write(reinterpret_cast<const char*>(salt.data()), 32);

            // Derive Key
            chrono_util::Bytes encryption_key = derive_key(passphrase, salt);

            // Generate IV
            chrono_util::Bytes iv(AESCrypto::IV_SIZE);
            if (!fill_secure_random(iv.data(), iv.size())) {
                LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to generate random IV for key: {}", key_id);
                return false;
            }
            file.write(reinterpret_cast<const char*>(iv.data()), AESCrypto::IV_SIZE);

            // Encrypt
            auto result = AESCrypto::encrypt(encryption_key, iv, private_key);
            if (!result) {
                LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Encryption failed for key: {}", key_id);
                return false;
            }

            // Result contains ciphertext + tag (appended)
            // Write Tag (last 16 bytes)
            size_t tag_size = AESCrypto::TAG_SIZE;
            size_t ciphertext_size = result->size() - tag_size;
            const uint8_t* tag_ptr = result->data() + ciphertext_size;
            file.write(reinterpret_cast<const char*>(tag_ptr), tag_size);

            // Write Ciphertext
            file.write(reinterpret_cast<const char*>(result->data()), ciphertext_size);
        }

        file.close();
        
        // Set restrictive permissions (readable/writable by owner only)
        std::filesystem::permissions(key_path, 
                                     std::filesystem::perms::owner_read | 
                                     std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace);
        
        LOG_INFO(chrono_util::LogCategory::CRYPTO, "Private key saved securely: {}", key_id);
        return true;
        
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Error saving private key: {}", e.what());
        return false;
    }
}

chrono_util::Bytes KeyManager::load_private_key(const std::string& key_id, const std::string& passphrase) {
    // Check cache first
    auto it = key_cache_.find(key_id);
    if (it != key_cache_.end()) {
        LOG_DEBUG(chrono_util::LogCategory::CRYPTO, "Loaded key from cache: {}", key_id);
        return it->second;
    }
    
    try {
        std::filesystem::path key_path = get_key_path(key_id);
        
        if (!std::filesystem::exists(key_path)) {
            LOG_WARN(chrono_util::LogCategory::CRYPTO, "Key file not found: {} ({})", key_id, key_path.string());
            return chrono_util::Bytes();
        }
        
        // Read key from file
        std::ifstream file(key_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to open key file: {}", key_path.string());
            return chrono_util::Bytes();
        }
        
        // Read entire file into Bytes
        file.seekg(0, std::ios::end);
        size_t file_size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        chrono_util::Bytes key(file_size);
        file.read(reinterpret_cast<char*>(key.data()), file_size);
        file.close();
        
        // Check magic
        if (file_size > 4 && std::memcmp(key.data(), "CKEY", 4) == 0) {
            // Encrypted
            if (passphrase.empty()) {
                LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Key {} is encrypted but no passphrase provided", key_id);
                return chrono_util::Bytes();
            }

            size_t offset = 4;
            // Read Salt
            if (offset + 32 > file_size) return chrono_util::Bytes();
            chrono_util::Bytes salt(key.begin() + offset, key.begin() + offset + 32);
            offset += 32;

            // Read IV
            if (offset + AESCrypto::IV_SIZE > file_size) return chrono_util::Bytes();
            chrono_util::Bytes iv(key.begin() + offset, key.begin() + offset + AESCrypto::IV_SIZE);
            offset += AESCrypto::IV_SIZE;

            // Read Tag
            if (offset + AESCrypto::TAG_SIZE > file_size) return chrono_util::Bytes();
            chrono_util::Bytes tag(key.begin() + offset, key.begin() + offset + AESCrypto::TAG_SIZE);
            offset += AESCrypto::TAG_SIZE;

            // Read Ciphertext
            chrono_util::Bytes ciphertext(key.begin() + offset, key.end());

            // Derive Key
            chrono_util::Bytes encryption_key = derive_key(passphrase, salt);

            // Decrypt
            auto plaintext = AESCrypto::decrypt(encryption_key, iv, ciphertext, tag);
            if (!plaintext) {
                LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Decryption failed for key: {} (Wrong passphrase?)", key_id);
                return chrono_util::Bytes();
            }
            
            key_cache_[key_id] = *plaintext;
            return *plaintext;
        } else {
            // Plaintext (Legacy)
            if (!passphrase.empty()) {
                LOG_WARN(chrono_util::LogCategory::CRYPTO, "Passphrase provided for plaintext key {}", key_id);
            }
            key_cache_[key_id] = key;
            return key;
        }
        
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Error loading private key: {}", e.what());
        return chrono_util::Bytes();
    }
}

std::string KeyManager::encode_public_key_base58(const chrono_util::Bytes& public_key) {
    return encode_base58check(public_key, "cqc");
}

chrono_util::Bytes KeyManager::decode_public_key_base58(const std::string& encoded_key) {
    return decode_base58check(encoded_key);
}

bool KeyManager::key_exists(const std::string& key_id) {
    return std::filesystem::exists(get_key_path(key_id));
}

std::vector<std::string> KeyManager::list_keys() {
    std::vector<std::string> keys;
    
    try {
        if (!std::filesystem::exists(key_directory_)) {
            return keys;
        }
        
        for (const auto& entry : std::filesystem::directory_iterator(key_directory_)) {
            if (entry.path().extension() == ".key") {
                // Extract key ID from filename (remove .key extension)
                std::string filename = entry.path().filename().string();
                std::string key_id = filename.substr(0, filename.length() - 4);
                keys.push_back(key_id);
            }
        }
        
        LOG_INFO(chrono_util::LogCategory::CRYPTO, "Found {} keys in directory", keys.size());
        
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Error listing keys: {}", e.what());
    }
    
    return keys;
}

bool KeyManager::import_key(const std::string& key_id, const chrono_util::Bytes& private_key, const std::string& passphrase) {
    if (key_exists(key_id)) {
        LOG_WARN(chrono_util::LogCategory::CRYPTO, "Key already exists: {}", key_id);
        return false;
    }
    return save_private_key(key_id, private_key, passphrase);
}

std::optional<chrono_util::Bytes> KeyManager::export_key(const std::string& key_id) {
    chrono_util::Bytes key = load_private_key(key_id);
    if (key.empty()) {
        return std::nullopt;
    }
    return key;
}

} // namespace chrono_crypto
