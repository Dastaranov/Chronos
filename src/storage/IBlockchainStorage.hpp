//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file IBlockchainStorage.hpp
 * @brief Defines the IBlockchainStorage interface for managing blockchain data.
 *
 * This abstract base class provides a contract for any class that needs to store
 * and retrieve blockchain-specific data, such as blocks and chain state. It abstracts
 * away the underlying storage mechanism, allowing for different implementations
 * (e.g., disk-based, in-memory) for full and light nodes.
 */

#pragma once

#include "ledger/block.hpp"
#include "util/bytes.hpp"
#include <optional>
#include <string> // For potential string keys for metadata

namespace chrono_storage {

/**
 * @class IBlockchainStorage
 * @brief Abstract base class for blockchain data storage.
 *
 * Provides methods to store, retrieve, and manage blockchain blocks and metadata.
 */
class IBlockchainStorage {
public:
    virtual ~IBlockchainStorage() = default;

    /**
     * @brief Saves a block to storage.
     * @param block The block to save.
     */
    virtual bool saveBlock(const chrono_ledger::Block& block) = 0;

    /**
     * @brief Retrieves a block by its hash.
     * @param block_hash The hash of the block to retrieve.
     * @return An optional containing the block if found, otherwise `std::nullopt`.
     */
    virtual std::optional<chrono_ledger::Block> getBlock(const chrono_util::Bytes& block_hash) const = 0;
    virtual std::optional<chrono_ledger::Block> getBlock(uint64_t height) const = 0;
    virtual bool hasBlock(const chrono_util::Bytes& hash) const = 0;

    /**
     * @brief Saves a key-value pair of blockchain-related metadata.
     * @param key The key for the metadata.
     * @param value The value of the metadata.
     */
    virtual bool saveMetadata(const chrono_util::Bytes& key, const chrono_util::Bytes& value) = 0;

    /**
     * @brief Retrieves blockchain-related metadata by key.
     * @param key The key for the metadata.
     * @return An optional containing the metadata value if found, otherwise `std::nullopt`.
     */
    virtual std::optional<chrono_util::Bytes> getMetadata(const chrono_util::Bytes& key) const = 0;

    // TODO: Add methods for iteration, pruning, etc., as needed.
};

} // namespace chrono_storage
