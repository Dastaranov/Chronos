//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file MemoryBlockchainStorage.cpp
 * @brief Implements the MemoryBlockchainStorage class for in-memory blockchain storage.
 */

#include "storage/MemoryBlockchainStorage.hpp"
#include "util/log.hpp" // For logging
#include "util/bytes.hpp" // For chrono_util::bytes_to_hex
#include <stdexcept>

namespace chrono_storage {

MemoryBlockchainStorage::MemoryBlockchainStorage(std::unique_ptr<IKv> kv_store)
    : kv_store_(std::move(kv_store)) {
    if (!kv_store_) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "MemoryBlockchainStorage initialized with a null IKv store.");
        throw std::runtime_error("IKv store cannot be null.");
    }
    LOG_INFO(chrono_util::LogCategory::STORAGE, "MemoryBlockchainStorage initialized.");
}

bool MemoryBlockchainStorage::saveBlock(const chrono_ledger::Block& block) {
    chrono_util::Bytes block_hash = block.get_header_hash();
    chrono_util::Bytes serialized_block = block.serialize();
    kv_store_->put(block_hash, serialized_block);

    // NEW: Save mapping from height to block hash
    chrono_util::Bytes height_key;
    height_key.push_back('_'); height_key.push_back('H'); height_key.push_back('_'); // Prefix for height key
    chrono_util::Bytes height_bytes(sizeof(uint64_t));
    std::memcpy(height_bytes.data(), &block.height, sizeof(uint64_t));
    height_key.insert(height_key.end(), height_bytes.begin(), height_bytes.end());
    kv_store_->put(height_key, block_hash);

    LOG_INFO(chrono_util::LogCategory::STORAGE, "Block saved to memory: {} at height {}", chrono_util::bytes_to_hex(block_hash), block.height);
    return true;
}

std::optional<chrono_ledger::Block> MemoryBlockchainStorage::getBlock(const chrono_util::Bytes& block_hash) const {
    std::optional<chrono_util::Bytes> serialized_block_data = kv_store_->get(block_hash);
    if (serialized_block_data) {
        try {
            return chrono_ledger::Block::deserialize(*serialized_block_data);
        } catch (const std::exception& e) {
            LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to deserialize block data from memory for hash {}: {}", 
                      chrono_util::bytes_to_hex(block_hash), e.what());
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool MemoryBlockchainStorage::saveMetadata(const chrono_util::Bytes& key, const chrono_util::Bytes& value) {
    kv_store_->put(key, value);
    LOG_INFO(chrono_util::LogCategory::STORAGE, "Metadata saved to memory: {}", chrono_util::bytes_to_hex(key));
    return true;
}

std::optional<chrono_util::Bytes> MemoryBlockchainStorage::getMetadata(const chrono_util::Bytes& key) const {
    return kv_store_->get(key);
}

std::optional<chrono_ledger::Block> MemoryBlockchainStorage::getBlock(uint64_t height) const {
    // Construct the key for height to hash mapping
    chrono_util::Bytes height_key;
    height_key.push_back('_'); height_key.push_back('H'); height_key.push_back('_'); // Prefix for height key
    chrono_util::Bytes height_bytes(sizeof(uint64_t));
    std::memcpy(height_bytes.data(), &height, sizeof(uint64_t));
    height_key.insert(height_key.end(), height_bytes.begin(), height_bytes.end());

    std::optional<chrono_util::Bytes> block_hash_data = kv_store_->get(height_key);
    if (block_hash_data) {
        return getBlock(*block_hash_data); // Use the existing getBlock by hash method
    }
    return std::nullopt;
}

bool MemoryBlockchainStorage::hasBlock(const chrono_util::Bytes& hash) const {
    return kv_store_->get(hash).has_value();
}

} // namespace chrono_storage
