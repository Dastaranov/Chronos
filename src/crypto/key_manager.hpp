//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file key_manager.hpp
 * @brief Defines the KeyManager class for secure key storage and retrieval.
 *
 * This class manages cryptographic keys (private and public) securely, storing them
 * in encrypted files rather than directly in configuration. It supports key registration,
 * retrieval, and encoding public keys in Base58Check format for user-friendly display.
 */

#pragma once

#include "util/bytes.hpp"
#include <string>
#include <unordered_map>
#include <memory>
#include <filesystem>

namespace chrono_crypto {

/**
 * @class KeyManager
 * @brief Manages cryptographic keys with secure file storage.
 *
 * Features:
 * - Store private keys in secure files instead of config
 * - Load keys by ID (e.g., "validator-1")
 * - Encode public keys in Base58Check format
 * - Prevent accidental exposure of sensitive keys
 */
class KeyManager {
public:
    /**
     * @brief Constructs a KeyManager with a key storage directory.
     * @param key_dir The directory where keys will be stored (e.g., ~/.chronos/keys)
     */
    explicit KeyManager(const std::string& key_dir);

    /**
     * @brief Saves a private key to disk with a given ID.
     * @param key_id A user-friendly identifier (e.g., "validator-1")
     * @param private_key The raw private key bytes
     * @return true if saved successfully, false otherwise
     */
    bool save_private_key(const std::string& key_id, const chrono_util::Bytes& private_key);

    /**
     * @brief Loads a private key from disk by ID.
     * @param key_id The identifier of the key to load
     * @return The loaded private key, or empty Bytes if not found
     */
    chrono_util::Bytes load_private_key(const std::string& key_id);

    /**
     * @brief Converts a public key to Base58Check format for user-friendly display.
     * @param public_key The raw public key bytes
     * @return Base58Check-encoded public key (shorter, with checksum)
     */
    std::string encode_public_key_base58(const chrono_util::Bytes& public_key);

    /**
     * @brief Decodes a Base58Check-encoded public key back to raw bytes.
     * @param encoded_key The Base58Check-encoded string
     * @return The decoded public key bytes, or empty if invalid
     */
    chrono_util::Bytes decode_public_key_base58(const std::string& encoded_key);

    /**
     * @brief Checks if a key with the given ID exists on disk.
     * @param key_id The identifier to check
     * @return true if the key file exists, false otherwise
     */
    bool key_exists(const std::string& key_id);

    /**
     * @brief Lists all key IDs available in the key directory.
     * @return A vector of key IDs
     */
    std::vector<std::string> list_keys();

private:
    std::filesystem::path key_directory_; ///< Directory where keys are stored
    std::unordered_map<std::string, chrono_util::Bytes> key_cache_; ///< In-memory cache for loaded keys

    /**
     * @brief Gets the full path for a key file by ID.
     * @param key_id The key identifier
     * @return Full filesystem path to the key file
     */
    std::filesystem::path get_key_path(const std::string& key_id);

    /**
     * @brief Ensures the key directory exists, creating it if necessary.
     * @return true if directory exists or was created, false on failure
     */
    bool ensure_key_directory();
};

} // namespace chrono_crypto
