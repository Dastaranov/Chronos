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
#include <optional> // Added for std::optional
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
     * @brief Struct to hold a key pair.
     */
    struct KeyPair {
        chrono_util::Bytes public_key;
        chrono_util::Bytes private_key;
    };

    /**
     * @brief Saves a key pair to disk with a given ID.
     * Saves private key to `key_id` and public key to `key_id.pub`.
     * @param key_id A user-friendly identifier
     * @param keys The key pair to save
     * @param passphrase Optional passphrase for encryption (only encrypts private key currently)
     * @return true if saved successfully, false otherwise
     */
    bool save_key_pair(const std::string& key_id, const KeyPair& keys, const std::string& passphrase = "");

    /**
     * @brief Loads a key pair from disk by ID.
     * Loads private key from `key_id` and public key from `key_id.pub`.
     * @param key_id The identifier of the key to load
     * @param passphrase Optional passphrase for decryption
     * @return The loaded key pair, or nullopt if not found or failure
     */
    std::optional<KeyPair> load_key_pair(const std::string& key_id, const std::string& passphrase = "");

    /**
     * @brief Saves a private key to disk with a given ID, optionally encrypted.
     * @param key_id A user-friendly identifier (e.g., "validator-1")
     * @param private_key The raw private key bytes
     * @param passphrase Optional passphrase for encryption. If empty, saves as plaintext (NOT RECOMMENDED).
     * @return true if saved successfully, false otherwise
     */
    bool save_private_key(const std::string& key_id, const chrono_util::Bytes& private_key, const std::string& passphrase = "");

    /**
     * @brief Loads a private key from disk by ID, optionally decrypting it.
     * @param key_id The identifier of the key to load
     * @param passphrase Optional passphrase for decryption.
     * @return The loaded private key, or empty Bytes if not found or decryption failed
     */
    chrono_util::Bytes load_private_key(const std::string& key_id, const std::string& passphrase = "");

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

    /**
     * @brief Imports a private key from raw bytes and saves it with the given ID.
     * @param key_id The identifier for the new key
     * @param private_key The raw private key bytes
     * @param passphrase Optional passphrase for encryption
     * @return true if imported successfully, false otherwise
     */
    bool import_key(const std::string& key_id, const chrono_util::Bytes& private_key, const std::string& passphrase = "");

    /**
     * @brief Exports a private key to raw bytes.
     * @param key_id The identifier of the key to export
     * @return The raw private key bytes, or empty if not found
     */
    std::optional<chrono_util::Bytes> export_key(const std::string& key_id);

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
