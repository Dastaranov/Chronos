//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file block.hpp
 * @brief This file defines the Block class, representing a fundamental unit in the Chronos blockchain ledger.
 *
 * The Block class encapsulates the structure and behavior of a blockchain block, including its header
 * (previous block hash, timestamp, Merkle root, height) and its body (a list of transactions).
 * It provides methods for block creation, hashing, serialization, deserialization, JSON conversion,
 * and validation. This class is crucial for maintaining the integrity and immutability of the blockchain.
 *
 * Key functionalities include:
 * - `Block()`: Default constructor.
 * - `Block(const Bytes& prev_hash, uint64_t height, const std::vector<Transaction>& txs)`: Constructor for creating a new block.
 * - `get_header_hash()`: Calculates the cryptographic hash of the block header.
 * - `calculate_merkle_root()`: Computes the Merkle root of all transactions within the block.
 * - `serialize()`: Converts the block into a byte vector for storage or network transmission.
 * - `deserialize(const Bytes& data)`: Reconstructs a Block object from a byte vector.
 * - `to_json()`: Converts the block's data into a JSON object.
 * - `from_json(const nlohmann::json& j)`: Populates the block's data from a JSON object.
 * - `is_valid()`: Performs validation checks on the block's integrity.
 */

#pragma once

#include "util/bytes.hpp"
#include "ledger/transaction.hpp"
#include <vector>
#include <cstdint>
#include <chrono>
#include <nlohmann/json.hpp> // NEW: For json support

namespace chrono_ledger {

/**
 * @class Block
 * @brief Represents a single block in the Chronos blockchain.
 *
 * A Block consists of a header and a body. The header contains metadata about the block,
 * such as its link to the previous block, timestamp, and a summary of its transactions.
 * The body contains the actual list of transactions included in this block.
 * This class provides all necessary operations for managing block data.
 */
class Block {
public:
    // Block Header
    Bytes prev_block_hash; ///< @var prev_block_hash The hash of the previous block in the blockchain, linking blocks together.
    uint64_t timestamp = 0; ///< @var timestamp The Unix timestamp (in milliseconds) when the block was created.
    Bytes transactions_merkle_root; ///< @var transactions_merkle_root The Merkle root of all transactions included in this block, ensuring transaction integrity.
    uint64_t height = 0; ///< @var height The block number or height in the blockchain, with the genesis block typically being 0.
    uint64_t consensus_time = 0; ///< @var consensus_time The aggregated Proof-of-Time consensus time when the block was created.
    uint32_t round = 0; ///< @var round The BFT round in which this block was proposed/finalized.
    uint32_t time_tier = 5; ///< @var time_tier The Time Tier of the block proposer (1=Quantum, 2=Atomic, ..., 5=NTP).
    uint32_t time_quality_score = 0; ///< @var time_quality_score The Time Quality Score of the proposer (0-100).

    // Block Body
    std::vector<Transaction> transactions; ///< @var transactions A list of all transactions included in this block.

    /**
     * @brief Default constructor for the Block class.
     *
     * Initializes a Block object with default values. This constructor is useful for creating
     * an empty block that can be populated later.
     */
    Block() = default;

    /**
     * @brief Constructor for creating a new block with specified details.
     *
     * This constructor initializes a new block with the hash of the previous block,
     * its height, and a list of transactions. The timestamp is automatically set to
     * the current time, and the Merkle root is calculated based on the provided transactions.
     *
     * @param prev_hash The hash of the preceding block.
     * @param height The height of this new block in the blockchain.
     * @param consensus_time The aggregated Proof-of-Time consensus time.
     * @param round The BFT round number.
     * @param time_tier The Time Tier of the proposer.
     * @param time_quality_score The Time Quality Score of the proposer.
     * @param txs A vector of `Transaction` objects to be included in this block.
     */
    Block(const Bytes& prev_hash, uint64_t height, uint64_t consensus_time, uint32_t round, uint32_t time_tier, uint32_t time_quality_score, const std::vector<Transaction>& txs);

    /**
     * @brief Calculates the cryptographic hash of the block header.
     *
     * This method serializes the block header (excluding the transactions body) and
     * computes its cryptographic hash (e.g., using BLAKE3). This hash uniquely
     * identifies the block and is used to link it to subsequent blocks.
     *
     * @return A `Bytes` object containing the hash of the block header.
     */
    Bytes get_header_hash() const;

    /**
     * @brief Calculates the Merkle root of the transactions in the block.
     *
     * This method constructs a Merkle tree from all transactions in the `transactions` vector
     * and returns the root hash of that tree. The Merkle root provides an efficient way to
     * verify the integrity of all transactions in the block.
     *
     * @return A `Bytes` object containing the Merkle root hash.
     */
    Bytes calculate_merkle_root() const;

    /**
     * @brief Serializes the entire block into a byte vector.
     *
     * This method converts the block's header and body into a compact byte representation.
     * This is essential for storing the block on disk, transmitting it over a network,
     * or for use in cryptographic hashing.
     *
     * @return A `Bytes` object containing the serialized representation of the block.
     */
    Bytes serialize() const;

    /**
     * @brief Deserializes a byte vector into a Block object.
     *
     * This static method reconstructs a `Block` object from its serialized byte representation.
     * It is the inverse operation of `serialize()`.
     *
     * @param data A `Bytes` object containing the serialized block data.
     * @return A `Block` object reconstructed from the input data.
     */
    static Block deserialize(const Bytes& data);

    /**
     * @brief Returns a canonical binary representation of the block for signing purposes.
     *
     * This method serializes the essential header fields of the block into a byte vector
     * that can be used as input for cryptographic signing.
     *
     * @return A `Bytes` object containing the canonical signable representation of the block.
     */
    chrono_util::Bytes get_signable_bytes() const;

    /**
     * @brief Checks if the block is valid.
     *
     * This method performs various validation checks to ensure the integrity and correctness
     * of the block. This typically includes verifying the Merkle root against the transactions
     * and potentially other consensus rules.
     *
     * @return `true` if the block is valid according to the defined rules, `false` otherwise.
     */
    bool is_valid() const;

    /**
     * @brief Converts the block's data into a JSON object.
     *
     * This method serializes the block's header and body into a JSON representation.
     *
     * @return A `nlohmann::json` object representing the block.
     */
    nlohmann::json to_json() const;

    /**
     * @brief Populates the block's data from a JSON object.
     *
     * This method deserializes a JSON object into the block's header and body fields.
     *
     * @param j A `nlohmann::json` object containing the block's data.
     */
    void from_json(const nlohmann::json& j);
};

} // namespace chrono_ledger
