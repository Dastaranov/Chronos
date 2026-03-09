#pragma once
#include "IBlockchainStorage.hpp"
#include <leveldb/db.h>
#include <memory>
#include <string>
#include <vector>

namespace chrono_storage {

class LevelDBBlockchainStorage : public IBlockchainStorage {
public:
    LevelDBBlockchainStorage(const std::string& db_path);
    ~LevelDBBlockchainStorage() override;
    
    bool saveBlock(const chrono_ledger::Block& block) override;
    std::optional<chrono_ledger::Block> getBlock(const chrono_util::Bytes& hash) const override;
    std::optional<chrono_ledger::Block> getBlock(uint64_t height) const override;
    bool hasBlock(const chrono_util::Bytes& hash) const override;
    
    bool saveMetadata(const chrono_util::Bytes& key, const chrono_util::Bytes& value) override;
    std::optional<chrono_util::Bytes> getMetadata(const chrono_util::Bytes& key) const override;
    
    // Batch operations for sync
    bool appendBlocks(const std::vector<chrono_ledger::Block>& blocks);
    
private:
    std::unique_ptr<leveldb::DB> db_;
    
    // Key schema helpers
    std::string height_key(uint64_t height) const;  // "h/<height>"
    std::string block_key(const chrono_util::Bytes& hash) const; // "b/<hash>"
    std::string meta_key(const chrono_util::Bytes& key) const;   // "m/<key>"
    
    // Serialization
    chrono_util::Bytes serialize_block_with_checksum(const chrono_ledger::Block& block) const;
    std::optional<chrono_ledger::Block> deserialize_block_with_validation(const chrono_util::Bytes& data) const;
};

} // namespace chrono_storage
