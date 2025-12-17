//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file MemoryBlockchainStorage.hpp
 * @brief Defines the MemoryBlockchainStorage class for in-memory blockchain storage.
 *
 * This class implements the IBlockchainStorage interface, providing functionalities
 * to store and retrieve blockchain blocks and metadata using an in-memory key-value store
 * (MemoryKv). It's designed for light nodes that require fast access to a limited
 * portion of the blockchain without persistent storage.
 */

#pragma once

#include "storage/IBlockchainStorage.hpp"
#include "storage/IKv.hpp" // For the underlying key-value store interface
#include "storage/memory_kv.hpp" // For MemoryKv implementation
#include "ledger/block.hpp"
#include "util/bytes.hpp"
#include <memory> // For std::unique_ptr

namespace chrono_storage {

/**
 * @class MemoryBlockchainStorage
 * @brief In-memory implementation of IBlockchainStorage.
 *
 * Stores blockchain blocks and metadata in volatile memory using an underlying MemoryKv store.
 */
class MemoryBlockchainStorage : public IBlockchainStorage {
public:
    /**
     * @brief Constructs a MemoryBlockchainStorage instance.
     * @param kv_store A unique pointer to an IKv implementation (e.g., MemoryKv) for underlying storage.
     */
    explicit MemoryBlockchainStorage(std::unique_ptr<IKv> kv_store);

    // IBlockchainStorage interface implementation
    void saveBlock(const chrono_ledger::Block& block) override;
    std::optional<chrono_ledger::Block> getBlock(const chrono_util::Bytes& block_hash) const override;
    std::optional<chrono_ledger::Block> getBlock(uint64_t height) const override; // NEW
    void saveMetadata(const chrono_util::Bytes& key, const chrono_util::Bytes& value) override;
    std::optional<chrono_util::Bytes> getMetadata(const chrono_util::Bytes& key) const override;

private:
    std::unique_ptr<IKv> kv_store_; ///< The underlying key-value store for in-memory persistence.
};

} // namespace chrono_storage
