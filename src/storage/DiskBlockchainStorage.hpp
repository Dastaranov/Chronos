//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file DiskBlockchainStorage.hpp
 * @brief Defines the DiskBlockchainStorage class for persistent, disk-based blockchain storage.
 *
 * This class implements the IBlockchainStorage interface, providing functionalities
 * to store and retrieve blockchain blocks and metadata using a persistent key-value store
 * (e.g., FileKv). It's designed for full nodes that require the entire blockchain
 * to be stored on disk.
 */

#pragma once

#include "storage/IBlockchainStorage.hpp"
#include "storage/IKv.hpp" // For the underlying key-value store
#include "ledger/block.hpp" // Assuming Block has serialization/deserialization
#include "util/bytes.hpp"
#include <memory> // For std::unique_ptr

namespace chrono_storage {

/**
 * @class DiskBlockchainStorage
 * @brief Disk-based implementation of IBlockchainStorage.
 *
 * Stores blockchain blocks and metadata persistently using an underlying IKv store (e.g., FileKv).
 */
class DiskBlockchainStorage : public IBlockchainStorage {
public:
    /**
     * @brief Constructs a DiskBlockchainStorage instance.
     * @param kv_store A unique pointer to an IKv implementation (e.g., FileKv) for underlying storage.
     */
    explicit DiskBlockchainStorage(std::unique_ptr<IKv> kv_store);

    // IBlockchainStorage interface implementation
    bool saveBlock(const chrono_ledger::Block& block) override;
    std::optional<chrono_ledger::Block> getBlock(const chrono_util::Bytes& block_hash) const override;
    std::optional<chrono_ledger::Block> getBlock(uint64_t height) const override;
    bool hasBlock(const chrono_util::Bytes& hash) const override;
    bool saveMetadata(const chrono_util::Bytes& key, const chrono_util::Bytes& value) override;
    std::optional<chrono_util::Bytes> getMetadata(const chrono_util::Bytes& key) const override;

private:
    std::unique_ptr<IKv> kv_store_; ///< The underlying key-value store for persistence.
};

} // namespace chrono_storage
