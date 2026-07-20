//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file block.cpp
 * @brief This file implements the Block class, which represents a fundamental unit in the Chronos blockchain ledger.
 *
 * The Block class implementation includes methods for constructing blocks, calculating their cryptographic hash,
 * computing the Merkle root of their transactions, and handling serialization/deserialization to and from
 * byte vectors and JSON objects. It also provides a mechanism for validating the block's integrity.
 *
 * Key functions implemented:
 * - `Block::Block`: Constructor for creating new blocks.
 * - `Block::get_header_hash`: Computes the BLAKE3 hash of the block header.
 * - `Block::calculate_merkle_root`: Computes the Merkle root of the block's transactions.
 * - `Block::serialize`: Converts the block into a byte stream.
 * - `Block::deserialize`: Reconstructs a block from a byte stream.
 * - `Block::to_json`: Converts the block into a JSON object.
 * - `Block::from_json`: Populates the block from a JSON object.
 * - `Block::is_valid`: Validates the block's internal consistency.
 */

#include "ledger/block.hpp"
#include "crypto/crypto_provider.hpp"
#include "util/codec.hpp"
#include "util/log.hpp"
#include <algorithm>
#include <stdexcept>
#include <cstring> // For memcpy
#include <chrono>

#include "util/bytes.hpp" // For bytes_to_hex and hex_to_bytes

namespace chrono_ledger {

namespace {
/**
 * @brief Normalizes layer_1_anchor to either empty or 32-byte hash form.
 * @param anchor Candidate anchor bytes.
 * @return 32-byte hash if present, empty bytes otherwise.
 */
Bytes normalize_layer_1_anchor(const Bytes& anchor) {
    if (anchor.empty()) {
        return {};
    }
    if (anchor.size() == 32) {
        return anchor;
    }
    Bytes normalized(32, 0);
    const size_t copy_size = std::min<size_t>(anchor.size(), normalized.size());
    std::copy(anchor.begin(), anchor.begin() + copy_size, normalized.begin());
    return normalized;
}
} // namespace

/**
 * @brief Constructor for creating a new block with specified details.
 *
 * This constructor initializes a new block with the hash of the previous block,
 * its height, and a list of transactions. The `timestamp` is automatically set to
 * the current system time (Unix timestamp in seconds), and the `transactions_merkle_root`
 * is calculated based on the provided transactions.
 *
 * @param prev_hash The hash of the preceding block, represented as a `Bytes` object.
 * @param height The height of this new block in the blockchain.
 * @param consensus_time_val The aggregated Proof-of-Time consensus time.
 * @param round_val The BFT round number.
 * @param time_tier_val The Time Tier of the proposer.
 * @param time_quality_score_val The Time Quality Score of the proposer.
 * @param txs A vector of `Transaction` objects to be included in this block.
 */
Block::Block(const Bytes& prev_hash, uint64_t height, uint64_t consensus_time_val, uint32_t round_val, uint32_t time_tier_val, uint32_t time_quality_score_val, const std::vector<Transaction>& txs, const Bytes& layer_1_anchor_val)
    : prev_block_hash(prev_hash), height(height), consensus_time(consensus_time_val), round(round_val), time_tier(time_tier_val), time_quality_score(time_quality_score_val), layer_1_anchor(normalize_layer_1_anchor(layer_1_anchor_val)), transactions(txs) {
    timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
    transactions_merkle_root = calculate_merkle_root();
}

/**
 * @brief Calculates the cryptographic hash of the block header.
 *
 * This method concatenates the `prev_block_hash`, `timestamp`, `transactions_merkle_root`,
 * `height`, `consensus_time`, `round`, and `layer_1_anchor` into a single byte array. It then computes the BLAKE3 hash of this combined
 * data to produce the unique identifier for the block header.
 *
 * @return A `Bytes` object containing the 32-byte BLAKE3 hash of the block header.
 */
Bytes Block::get_header_hash() const {
    Bytes header_data;
    header_data.insert(header_data.end(), prev_block_hash.begin(), prev_block_hash.end());

    Bytes timestamp_bytes(sizeof(timestamp));
    std::memcpy(timestamp_bytes.data(), &timestamp, sizeof(timestamp));
    header_data.insert(header_data.end(), timestamp_bytes.begin(), timestamp_bytes.end());

    header_data.insert(header_data.end(), transactions_merkle_root.begin(), transactions_merkle_root.end());

    Bytes height_bytes(sizeof(height));
    std::memcpy(height_bytes.data(), &height, sizeof(height));
    header_data.insert(header_data.end(), height_bytes.begin(), height_bytes.end());

    Bytes consensus_time_bytes(sizeof(consensus_time));
    std::memcpy(consensus_time_bytes.data(), &consensus_time, sizeof(consensus_time));
    header_data.insert(header_data.end(), consensus_time_bytes.begin(), consensus_time_bytes.end());

    Bytes round_bytes(sizeof(round));
    std::memcpy(round_bytes.data(), &round, sizeof(round));
    header_data.insert(header_data.end(), round_bytes.begin(), round_bytes.end());

    Bytes time_tier_bytes(sizeof(time_tier));
    std::memcpy(time_tier_bytes.data(), &time_tier, sizeof(time_tier));
    header_data.insert(header_data.end(), time_tier_bytes.begin(), time_tier_bytes.end());

    Bytes time_quality_score_bytes(sizeof(time_quality_score));
    std::memcpy(time_quality_score_bytes.data(), &time_quality_score, sizeof(time_quality_score));
    header_data.insert(header_data.end(), time_quality_score_bytes.begin(), time_quality_score_bytes.end());

    const Bytes anchor_bytes = normalize_layer_1_anchor(layer_1_anchor);
    header_data.insert(header_data.end(), anchor_bytes.begin(), anchor_bytes.end());

    return chrono_crypto::get_crypto_provider()->hash(header_data);
}

/**
 * @brief Calculates the Merkle root of the transactions in the block.
 *
 * Uses domain separation to distinguish merkle trees from other hash uses.
 * 
 * Algorithm:
 * - If no transactions: return Hash of domain-separated empty marker
 * - Hash each transaction: Hash(domain || tx.serialize())
 * - Build tree: pair hashes and hash again with domain separation
 * - Duplicate last hash if odd number of children
 * - Continue until single root hash
 *
 * Domain separation:
 * - Leaf level: "CHRONOS_TX_LEAF_" prefix
 * - Internal nodes: "CHRONOS_TX_NODE_" prefix
 *
 * @return A `Bytes` object containing the Merkle root hash.
 */
Bytes Block::calculate_merkle_root() const {
    auto crypto_provider = chrono_crypto::get_crypto_provider();

    if (transactions.empty()) {
        // Domain-separated empty block merkle root
        chrono_util::Bytes empty_marker = chrono_util::string_to_bytes("CHRONOS_MERKLE_EMPTY");
        return crypto_provider->hash(empty_marker);
    }

    // Hash each transaction with domain separation
    std::vector<Bytes> current_level_hashes;
    current_level_hashes.reserve(transactions.size());
    
    for (const auto& tx : transactions) {
        chrono_util::Bytes tx_leaf_data = chrono_util::string_to_bytes("CHRONOS_TX_LEAF_");
        chrono_util::Bytes tx_bytes = tx.serialize();
        tx_leaf_data.insert(tx_leaf_data.end(), tx_bytes.begin(), tx_bytes.end());
        current_level_hashes.push_back(crypto_provider->hash(tx_leaf_data));
    }

    // Build merkle tree with domain separation on internal nodes
    while (current_level_hashes.size() > 1) {
        std::vector<Bytes> next_level_hashes;
        next_level_hashes.reserve((current_level_hashes.size() + 1) / 2);
        
        for (size_t i = 0; i < current_level_hashes.size(); i += 2) {
            chrono_util::Bytes node_data = chrono_util::string_to_bytes("CHRONOS_TX_NODE_");
            node_data.insert(node_data.end(), current_level_hashes[i].begin(), current_level_hashes[i].end());
            
            if (i + 1 < current_level_hashes.size()) {
                // Pair with next hash
                node_data.insert(node_data.end(), current_level_hashes[i+1].begin(), current_level_hashes[i+1].end());
            } else {
                // Odd node: duplicate last hash
                node_data.insert(node_data.end(), current_level_hashes[i].begin(), current_level_hashes[i].end());
            }
            
            next_level_hashes.push_back(crypto_provider->hash(node_data));
        }
        
        current_level_hashes = next_level_hashes;
    }
    
    return current_level_hashes[0];
}

/**
 * @brief Serializes the entire block into a byte vector using canonical format.
 *
 * Serialization format (canonical, little-endian):
 * - prev_block_hash (32 bytes fixed)
 * - timestamp (uint64_t LE)
 * - transactions_merkle_root (32 bytes fixed)
 * - height (uint64_t LE)
 * - consensus_time (uint64_t LE)
 * - round (uint32_t LE)
 * - layer_1_anchor_length (uint32_t LE)
 * - layer_1_anchor (variable, usually 32-byte hash)
 * - num_transactions (uint32_t LE, not size_t)
 * - For each transaction:
 *   - transaction_size (uint32_t LE)
 *   - transaction_data (variable)
 *
 * @return A `Bytes` object containing the serialized representation of the block.
 */
Bytes Block::serialize() const {
    Bytes serialized_data;

    // Add prev_block_hash (fixed 32 bytes)
    serialized_data.insert(serialized_data.end(), prev_block_hash.begin(), prev_block_hash.end());

    // Add timestamp (uint64_t LE)
    chrono_util::write_fixed_uint64_le(timestamp, serialized_data);

    // Add transactions_merkle_root (fixed 32 bytes)
    serialized_data.insert(serialized_data.end(), transactions_merkle_root.begin(), transactions_merkle_root.end());

    // Add height (uint64_t LE)
    chrono_util::write_fixed_uint64_le(height, serialized_data);

    // Add consensus_time (uint64_t LE)
    chrono_util::write_fixed_uint64_le(consensus_time, serialized_data);

    // Add round (uint32_t LE)
    chrono_util::write_fixed_uint32_le(round, serialized_data);

    // Add time_tier (uint32_t LE)
    chrono_util::write_fixed_uint32_le(time_tier, serialized_data);

    // Add time_quality_score (uint32_t LE)
    chrono_util::write_fixed_uint32_le(time_quality_score, serialized_data);

    // Add layer_1_anchor with length prefix
    const Bytes anchor_bytes = normalize_layer_1_anchor(layer_1_anchor);
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(anchor_bytes.size()), serialized_data);
    serialized_data.insert(serialized_data.end(), anchor_bytes.begin(), anchor_bytes.end());

    // Add transaction count (uint32_t LE, not size_t for canonical format)
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(transactions.size()), serialized_data);

    // Add each transaction
    for (const auto& tx : transactions) {
        Bytes serialized_tx = tx.serialize();
        // Add transaction size (uint32_t LE)
        chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(serialized_tx.size()), serialized_data);
        // Add transaction data
        serialized_data.insert(serialized_data.end(), serialized_tx.begin(), serialized_tx.end());
    }

    return serialized_data;
}

/**
 * @brief Deserializes a byte vector into a Block object.
 *
 * This static method reconstructs a `Block` object from its serialized byte representation
 * using canonical format (little-endian, uint32_t for counts, not size_t).
 *
 * Expected format:
 * - prev_block_hash (32 bytes fixed)
 * - timestamp (uint64_t LE)
 * - transactions_merkle_root (32 bytes fixed)
 * - height (uint64_t LE)
 * - consensus_time (uint64_t LE)
 * - layer_1_anchor_length (uint32_t LE, optional for backward compatibility)
 * - layer_1_anchor (variable)
 * - num_transactions (uint32_t LE)
 * - For each transaction:
 *   - transaction_size (uint32_t LE)
 *   - transaction_data (variable)
 *
 * @param data A `Bytes` object containing the serialized block data.
 * @return A `Block` object reconstructed from the input data.
 * @throw std::runtime_error if the input data is too short or malformed.
 */
Block Block::deserialize(const Bytes& data) {
    size_t offset = 0;

    // Minimum size check: 32 (prev_hash) + 8 (timestamp) + 32 (merkle) + 8 (height) + 8 (consensus_time) + 4 (round) + 4 (time_tier) + 4 (tx_count)
    const size_t MIN_BLOCK_SIZE = 32 + 8 + 32 + 8 + 8 + 4 + 4 + 4;
    if (data.size() < MIN_BLOCK_SIZE) {
        throw std::runtime_error("Block data too short for deserialization.");
    }

    // Read prev_block_hash (32 bytes fixed)
    Bytes prev_block_hash_val(data.begin() + offset, data.begin() + offset + 32);
    offset += 32;

    // Read timestamp (uint64_t LE)
    uint64_t timestamp_val = chrono_util::read_fixed_uint64_le(data, offset);

    // Read transactions_merkle_root (32 bytes fixed)
    Bytes transactions_merkle_root_val(data.begin() + offset, data.begin() + offset + 32);
    offset += 32;

    // Read height (uint64_t LE)
    uint64_t height_val = chrono_util::read_fixed_uint64_le(data, offset);

    // Read consensus_time (uint64_t LE)
    uint64_t consensus_time_val = chrono_util::read_fixed_uint64_le(data, offset);

    // Read round (uint32_t LE)
    uint32_t round_val = chrono_util::read_fixed_uint32_le(data, offset);

    // Read time_tier (uint32_t LE)
    uint32_t time_tier_val = chrono_util::read_fixed_uint32_le(data, offset);

    // Read time_quality_score (uint32_t LE)
    uint32_t time_quality_score_val = chrono_util::read_fixed_uint32_le(data, offset);

    Bytes layer_1_anchor_val;
    std::vector<Transaction> transactions_val;

    const size_t tx_start_offset = offset;
    bool parsed_with_anchor = false;
    if (offset + 4 <= data.size()) {
        try {
            size_t trial_offset = offset;
            uint32_t anchor_len = chrono_util::read_fixed_uint32_le(data, trial_offset);
            if (trial_offset + anchor_len <= data.size()) {
                layer_1_anchor_val.assign(data.begin() + trial_offset, data.begin() + trial_offset + anchor_len);
                trial_offset += anchor_len;

                uint32_t num_tx = chrono_util::read_fixed_uint32_le(data, trial_offset);
                std::vector<Transaction> trial_transactions;
                trial_transactions.reserve(num_tx);

                for (uint32_t i = 0; i < num_tx; ++i) {
                    uint32_t tx_size = chrono_util::read_fixed_uint32_le(data, trial_offset);
                    if (trial_offset + tx_size > data.size()) {
                        throw std::runtime_error("Block data too short for transaction (anchor mode).");
                    }
                    Bytes tx_data(data.begin() + trial_offset, data.begin() + trial_offset + tx_size);
                    trial_transactions.push_back(Transaction::deserialize(tx_data));
                    trial_offset += tx_size;
                }

                if (trial_offset == data.size()) {
                    parsed_with_anchor = true;
                    offset = trial_offset;
                    transactions_val = std::move(trial_transactions);
                }
            }
        } catch (const std::exception&) {
            parsed_with_anchor = false;
        }
    }

    if (!parsed_with_anchor) {
        offset = tx_start_offset;
        uint32_t num_tx = chrono_util::read_fixed_uint32_le(data, offset);
        transactions_val.reserve(num_tx);

        for (uint32_t i = 0; i < num_tx; ++i) {
            uint32_t tx_size = chrono_util::read_fixed_uint32_le(data, offset);
            if (offset + tx_size > data.size()) {
                throw std::runtime_error("Block data too short for transaction at index " + std::to_string(i));
            }
            Bytes tx_data(data.begin() + offset, data.begin() + offset + tx_size);
            transactions_val.push_back(Transaction::deserialize(tx_data));
            offset += tx_size;
        }
    }

    // Create block and set fields
    Block block(prev_block_hash_val, height_val, consensus_time_val, round_val, time_tier_val, time_quality_score_val, transactions_val, layer_1_anchor_val);
    block.timestamp = timestamp_val;
    block.transactions_merkle_root = transactions_merkle_root_val;
    
    // Verify merkle root consistency: recompute and compare with stored
    Bytes computed_merkle = block.calculate_merkle_root();
    if (computed_merkle != transactions_merkle_root_val) {
        throw std::runtime_error(
            "Block deserialization failed: Merkle root mismatch. "
            "Expected: " + chrono_util::bytes_to_hex(transactions_merkle_root_val) + 
            ", computed: " + chrono_util::bytes_to_hex(computed_merkle)
        );
    }
    
    return block;
}

// NEW: Implement to_json and from_json for Block
nlohmann::json Block::to_json() const {
    nlohmann::json j;
    j["prev_block_hash"] = chrono_util::bytes_to_hex(prev_block_hash);
    j["timestamp"] = timestamp;
    j["transactions_merkle_root"] = chrono_util::bytes_to_hex(transactions_merkle_root);
    j["height"] = height;
    j["consensus_time"] = consensus_time;
    j["round"] = round;
    j["time_tier"] = time_tier;
    j["time_quality_score"] = time_quality_score;
    j["layer_1_anchor"] = chrono_util::bytes_to_hex(normalize_layer_1_anchor(layer_1_anchor));

    nlohmann::json txs_json = nlohmann::json::array();
    for (const auto& tx : transactions) {
        txs_json.push_back(tx.to_json());
    }
    j["transactions"] = txs_json;
    return j;
}

void Block::from_json(const nlohmann::json& j) {
    prev_block_hash = chrono_util::hex_to_bytes(j.at("prev_block_hash").get<std::string>());
    timestamp = j.at("timestamp").get<uint64_t>();
    transactions_merkle_root = chrono_util::hex_to_bytes(j.at("transactions_merkle_root").get<std::string>());
    height = j.at("height").get<uint64_t>();
    consensus_time = j.at("consensus_time").get<uint64_t>();
    round = j.at("round").get<uint32_t>();
    time_tier = j.value("time_tier", 5); // Default to 5 (NTP) if missing
    time_quality_score = j.value("time_quality_score", 0); // Default to 0 if missing
    layer_1_anchor = normalize_layer_1_anchor(chrono_util::hex_to_bytes(j.value("layer_1_anchor", "")));

    transactions.clear();
    for (const auto& tx_json : j.at("transactions")) {
        Transaction tx;
        tx.from_json(tx_json);
        transactions.push_back(tx);
    }
}


/**
 * @brief Returns a canonical binary representation of the block for signing purposes.
 *
 * This method serializes the essential header fields of the block into a byte vector
 * that can be used as input for cryptographic signing.
 *
 * @return A `Bytes` object containing the canonical signable representation of the block.
 */
chrono_util::Bytes Block::get_signable_bytes() const {
    Bytes signable_data;
    signable_data.insert(signable_data.end(), prev_block_hash.begin(), prev_block_hash.end());

    Bytes timestamp_bytes(sizeof(timestamp));
    std::memcpy(timestamp_bytes.data(), &timestamp, sizeof(timestamp));
    signable_data.insert(signable_data.end(), timestamp_bytes.begin(), timestamp_bytes.end());

    Bytes transactions_merkle_root_bytes(transactions_merkle_root.size());
    std::memcpy(transactions_merkle_root_bytes.data(), transactions_merkle_root.data(), transactions_merkle_root.size());
    signable_data.insert(signable_data.end(), transactions_merkle_root_bytes.begin(), transactions_merkle_root_bytes.end());

    Bytes height_bytes(sizeof(height));
    std::memcpy(height_bytes.data(), &height, sizeof(height));
    signable_data.insert(signable_data.end(), height_bytes.begin(), height_bytes.end());

    Bytes consensus_time_bytes(sizeof(consensus_time));
    std::memcpy(consensus_time_bytes.data(), &consensus_time, sizeof(consensus_time));
    signable_data.insert(signable_data.end(), consensus_time_bytes.begin(), consensus_time_bytes.end());

    Bytes round_bytes(sizeof(round));
    std::memcpy(round_bytes.data(), &round, sizeof(round));
    signable_data.insert(signable_data.end(), round_bytes.begin(), round_bytes.end());

    Bytes time_tier_bytes(sizeof(time_tier));
    std::memcpy(time_tier_bytes.data(), &time_tier, sizeof(time_tier));
    signable_data.insert(signable_data.end(), time_tier_bytes.begin(), time_tier_bytes.end());

    Bytes time_quality_score_bytes(sizeof(time_quality_score));
    std::memcpy(time_quality_score_bytes.data(), &time_quality_score, sizeof(time_quality_score));
    signable_data.insert(signable_data.end(), time_quality_score_bytes.begin(), time_quality_score_bytes.end());

    const Bytes anchor_bytes = normalize_layer_1_anchor(layer_1_anchor);
    signable_data.insert(signable_data.end(), anchor_bytes.begin(), anchor_bytes.end());

    return signable_data;
}

/**
 * @brief Checks if the block is valid.
 *
 * This method performs various validation checks to ensure the internal consistency
 * and correctness of the block. Currently, it verifies that the `transactions_merkle_root`
 * stored in the block header matches the Merkle root calculated from the actual transactions
 * in the block body. It also iterates through all transactions to ensure each one is valid.
 *
 * @return `true` if the block passes all internal validation checks, `false` otherwise.
 */
bool Block::is_valid() const {
    // Check prev_block_hash size (must be 32 bytes for BLAKE3)
    if (prev_block_hash.size() != 32) {
        LOG_WARN(chrono_util::LogCategory::LEDGER, 
                 "Block validation failed: invalid prev_block_hash size {} (expected 32)", 
                 prev_block_hash.size());
        return false;
    }

    // Check transactions_merkle_root size (must be 32 bytes)
    if (transactions_merkle_root.size() != 32) {
        LOG_WARN(chrono_util::LogCategory::LEDGER, 
                 "Block validation failed: invalid merkle_root size {} (expected 32)", 
                 transactions_merkle_root.size());
        return false;
    }

    // Check time_tier validity. Tier 5 (unauthenticated NTP) is the minimum accepted level.
    // Production validators should use tier 4 (NTS) or better, but we allow tier 5 for
    // testnet/development nodes that lack NTS infrastructure.
    if (time_tier == 0 || time_tier > 5) {
        LOG_WARN(chrono_util::LogCategory::LEDGER, 
                 "Block validation failed: invalid time_tier {} (must be 1-5)", 
                 time_tier);
        return false;
    }

    // Validate Time Quality Score against Time Tier
    uint32_t min_score = 0;
    switch (time_tier) {
        case 1: min_score = 95; break;
        case 2: min_score = 90; break;
        case 3: min_score = 80; break;
        case 4: min_score = 60; break;
        default: min_score = 0; break;
    }

    if (time_quality_score < min_score) {
        LOG_WARN(chrono_util::LogCategory::LEDGER, 
                 "Block validation failed: time_quality_score {} insufficient for tier {} (required {})", 
                 time_quality_score, time_tier, min_score);
        return false;
    }

    // Verify merkle root matches computed root
    Bytes computed_merkle = calculate_merkle_root();
    if (transactions_merkle_root != computed_merkle) {
        LOG_WARN(chrono_util::LogCategory::LEDGER, 
                 "Block validation failed: Merkle root mismatch at height {}. Expected: {}, computed: {}", 
                 height, 
                 chrono_util::bytes_to_hex(transactions_merkle_root),
                 chrono_util::bytes_to_hex(computed_merkle));
        return false;
    }

    // Validate all transactions
    for (size_t i = 0; i < transactions.size(); ++i) {
        if (!transactions[i].is_valid()) {
            LOG_WARN(chrono_util::LogCategory::LEDGER, 
                     "Block validation failed: transaction {} at height {} is invalid", 
                     i, height);
            return false;
        }
    }

    return true;
}

} // namespace chrono_ledger
    if (!layer_1_anchor.empty() && layer_1_anchor.size() != 32) {
        LOG_WARN(chrono_util::LogCategory::LEDGER,
                 "Block validation failed: invalid layer_1_anchor size {} (expected 0 or 32)",
                 layer_1_anchor.size());
        return false;
    }
