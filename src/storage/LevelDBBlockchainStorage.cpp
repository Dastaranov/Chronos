#include "LevelDBBlockchainStorage.hpp"
#include "ledger/block.hpp"
#include "util/log.hpp"
#include "util/codec.hpp"  // For LE encoding
#include "util/bytes.hpp"
#include "proto/ledger.pb.h" // Protobuf definition
#include <leveldb/write_batch.h>
#include <blake3.h>

namespace chrono_storage {

using namespace chrono_util;
using namespace chrono_ledger;

// Helper to convert Domain Block to Proto Block
chrono_ledger_proto::Block to_proto(const Block& block) {
    chrono_ledger_proto::Block pb_block;
    pb_block.set_prev_block_hash(std::string(block.prev_block_hash.begin(), block.prev_block_hash.end()));
    pb_block.set_timestamp(block.timestamp);
    pb_block.set_transactions_merkle_root(std::string(block.transactions_merkle_root.begin(), block.transactions_merkle_root.end()));
    pb_block.set_height(block.height);
    pb_block.set_consensus_time(block.consensus_time);
    pb_block.set_round(block.round);
    pb_block.set_time_tier(block.time_tier);
    pb_block.set_time_quality_score(block.time_quality_score);

    for (const auto& tx : block.transactions) {
        auto* pb_tx = pb_block.add_transactions();
        pb_tx->set_type(static_cast<uint32_t>(tx.type));
        pb_tx->set_sender(tx.sender.to_string());
        pb_tx->set_recipient(tx.recipient.to_string());
        pb_tx->set_amount(tx.amount);
        pb_tx->set_fee(tx.fee);
        pb_tx->set_nonce(tx.nonce);
        pb_tx->set_payload(std::string(tx.payload.begin(), tx.payload.end()));
        pb_tx->set_signature(std::string(tx.signature.begin(), tx.signature.end()));
    }
    return pb_block;
}

// Helper to convert Proto Block to Domain Block
Block from_proto(const chrono_ledger_proto::Block& pb_block) {
    std::vector<Transaction> transactions;
    for (const auto& pb_tx : pb_block.transactions()) {
        Transaction tx;
        tx.type = static_cast<TransactionType>(pb_tx.type());
        tx.sender = chrono_address::Address(pb_tx.sender());
        tx.recipient = chrono_address::Address(pb_tx.recipient());
        tx.amount = pb_tx.amount();
        tx.fee = pb_tx.fee();
        tx.nonce = pb_tx.nonce();
        tx.payload = chrono_util::string_to_bytes(pb_tx.payload());
        tx.signature = chrono_util::string_to_bytes(pb_tx.signature());
        transactions.push_back(tx);
    }

    Bytes prev_hash = chrono_util::string_to_bytes(pb_block.prev_block_hash());
    Block block(prev_hash, pb_block.height(), pb_block.consensus_time(), pb_block.round(), pb_block.time_tier(), pb_block.time_quality_score(), transactions);
    block.timestamp = pb_block.timestamp();
    block.transactions_merkle_root = chrono_util::string_to_bytes(pb_block.transactions_merkle_root());
    
    return block;
}

LevelDBBlockchainStorage::LevelDBBlockchainStorage(const std::string& db_path) {
    leveldb::Options options;
    options.create_if_missing = true;
    options.compression = leveldb::kSnappyCompression;
    
    leveldb::DB* db_raw;
    leveldb::Status status = leveldb::DB::Open(options, db_path, &db_raw);
    
    if (!status.ok()) {
        throw std::runtime_error("Failed to open LevelDB: " + status.ToString());
    }
    
    db_.reset(db_raw);
    LOG_INFO(chrono_util::LogCategory::STORAGE, "LevelDB opened at {}", db_path);
}

LevelDBBlockchainStorage::~LevelDBBlockchainStorage() {
    // db_ unique_ptr will close DB
}

std::string LevelDBBlockchainStorage::height_key(uint64_t height) const {
    Bytes height_bytes;
    write_fixed_uint64_le(height, height_bytes);
    return "h/" + std::string(height_bytes.begin(), height_bytes.end());
}

std::string LevelDBBlockchainStorage::block_key(const Bytes& hash) const {
    return "b/" + std::string(hash.begin(), hash.end());
}

std::string LevelDBBlockchainStorage::meta_key(const Bytes& key) const {
    return "m/" + std::string(key.begin(), key.end());
}

bool LevelDBBlockchainStorage::saveBlock(const Block& block) {
    // Use Protobuf serialization
    chrono_ledger_proto::Block pb_block = to_proto(block);
    std::string block_data_str;
    if (!pb_block.SerializeToString(&block_data_str)) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to serialize block to protobuf");
        return false;
    }
    
    Bytes block_hash = block.get_header_hash();
    uint64_t height = block.height;
    
    leveldb::WriteBatch batch;
    
    // Write block: "b/<hash>" -> protobuf_bytes
    batch.Put(block_key(block_hash), block_data_str);
    
    // Write height index: "h/<height>" -> hash
    std::string height_k = height_key(height);
    batch.Put(height_k,
              leveldb::Slice(reinterpret_cast<const char*>(block_hash.data()), block_hash.size()));
    
    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    
    if (!status.ok()) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to save block: {}", status.ToString());
        return false;
    }
    
    return true;
}

std::optional<Block> LevelDBBlockchainStorage::getBlock(const Bytes& hash) const {
    std::string value;
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), block_key(hash), &value);
    
    if (!status.ok()) {
        return std::nullopt;
    }
    
    try {
        chrono_ledger_proto::Block pb_block;
        if (!pb_block.ParseFromString(value)) {
             LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to parse block from protobuf");
             return std::nullopt;
        }
        return from_proto(pb_block);
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to deserialize block: {}", e.what());
        return std::nullopt;
    }
}

std::optional<Block> LevelDBBlockchainStorage::getBlock(uint64_t height) const {
    std::string value;
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), height_key(height), &value);
    
    if (!status.ok()) {
        return std::nullopt;
    }
    
    Bytes block_hash(value.begin(), value.end());
    return getBlock(block_hash);
}

bool LevelDBBlockchainStorage::hasBlock(const Bytes& hash) const {
    std::string value;
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), block_key(hash), &value);
    return status.ok();
}

bool LevelDBBlockchainStorage::saveMetadata(const Bytes& key, const Bytes& value) {
    leveldb::Status status = db_->Put(leveldb::WriteOptions(), meta_key(key), 
                                      leveldb::Slice(reinterpret_cast<const char*>(value.data()), value.size()));
    return status.ok();
}

std::optional<Bytes> LevelDBBlockchainStorage::getMetadata(const Bytes& key) const {
    std::string value;
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), meta_key(key), &value);
    
    if (!status.ok()) {
        return std::nullopt;
    }
    
    return Bytes(value.begin(), value.end());
}

bool LevelDBBlockchainStorage::appendBlocks(const std::vector<Block>& blocks) {
    leveldb::WriteBatch batch;
    for (const auto& block : blocks) {
        chrono_ledger_proto::Block pb_block = to_proto(block);
        std::string block_data_str;
        pb_block.SerializeToString(&block_data_str);

        Bytes block_hash = block.get_header_hash();
        uint64_t height = block.height;
        
        batch.Put(block_key(block_hash), block_data_str);
        
        std::string height_k = height_key(height);
        batch.Put(height_k,
                  leveldb::Slice(reinterpret_cast<const char*>(block_hash.data()), block_hash.size()));
    }
    
    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    return status.ok();
}

} // namespace chrono_storage
