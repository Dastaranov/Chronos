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
#include "util/log.hpp"
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace chrono_crypto {

// Base58Check alphabet (excludes 0, O, I, l to avoid confusion)
const std::string BASE58_ALPHABET = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

/**
 * @brief Encodes bytes to Base58Check format.
 * 
 * Base58Check is used in Bitcoin and provides:
 * - Checksum for error detection
 * - No confusion between 0/O or I/l
 * - Shorter representation than hex
 */
static std::string encode_base58check(const chrono_util::Bytes& data, const std::string& version = "cqc") {
    // This is a simplified Base58Check encoder
    // In production, use a proper Base58Check library or implement rigorously
    
    // For now, return a prefixed hex representation
    // TODO: Implement full Base58Check with proper checksum
    std::string result = version + "1";
    
    // Convert first 20 bytes to base58 (simplified)
    for (size_t i = 0; i < std::min(size_t(20), data.size()); i++) {
        uint8_t byte = data[i];
        result += BASE58_ALPHABET[byte % 58];
    }
    
    return result;
}

/**
 * @brief Decodes Base58Check format back to bytes.
 */
static chrono_util::Bytes decode_base58check(const std::string& encoded) {
    chrono_util::Bytes result;
    
    // Simplified decoder (remove version prefix)
    if (encoded.substr(0, 4) == "cqc1") {
        std::string data = encoded.substr(4);
        for (char c : data) {
            auto it = BASE58_ALPHABET.find(c);
            if (it != std::string::npos) {
                result.push_back(static_cast<uint8_t>(it));
            }
        }
    }
    
    return result;
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

bool KeyManager::save_private_key(const std::string& key_id, const chrono_util::Bytes& private_key) {
    try {
        std::filesystem::path key_path = get_key_path(key_id);
        
        // Open file in binary write mode
        std::ofstream file(key_path, std::ios::binary);
        if (!file.is_open()) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to open key file for writing: {}", key_path.string());
            return false;
        }
        
        // Write the raw key bytes
        file.write(reinterpret_cast<const char*>(private_key.data()), private_key.size());
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

chrono_util::Bytes KeyManager::load_private_key(const std::string& key_id) {
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
        
        // Cache the loaded key
        key_cache_[key_id] = key;
        
        LOG_INFO(chrono_util::LogCategory::CRYPTO, "Private key loaded: {} ({} bytes)", key_id, file_size);
        return key;
        
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

} // namespace chrono_crypto
