//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file node_status.hpp
 * @brief Defines the NodeStatus struct to hold displayable information about a Chronos node.
 *
 * This struct centralizes the key metrics and state variables that are relevant
 * for a user-friendly console display of the node's operation.
 */

#pragma once

#include <string>
#include <atomic>

namespace chrono_node {

/**
 * @struct NodeStatus
 * @brief Holds the current operational status and metrics of a Chronos node.
 *
 * This struct is designed to be easily updated and read by different parts of the
 * node application, providing a snapshot of its current state for display purposes.
 */
struct NodeStatus {
    std::string node_id = "N/A"; ///< Unique identifier for the node.
    std::string rpc_address = "N/A"; ///< RPC listening address (e.g., "127.0.0.1:8080").
    std::string p2p_address = "N/A"; ///< P2P listening address (e.g., "127.0.0.1:9000").
    std::string data_dir = "data"; ///< Data directory for blockchain storage.
    std::string storage_size = "0 MB"; ///< Current blockchain storage size.
    std::atomic<uint64_t> current_block_height = 0; ///< The height of the latest block processed.
    std::atomic<size_t> mempool_size = 0; ///< Number of transactions currently in the mempool.
    std::atomic<size_t> connected_peers = 0; ///< Number of active P2P connections.
    std::atomic<uint64_t> total_transactions_processed = 0; ///< Total transactions processed since node start.
    std::atomic<uint64_t> total_blocks_processed = 0; ///< Total blocks processed since node start.
    std::atomic<uint32_t> active_validators = 0; ///< Number of currently active validators.
    std::atomic<uint32_t> pending_votes = 0; ///< Number of pending governance votes.
    std::atomic<uint32_t> consensus_round = 0; ///< Current BFT consensus round.
    std::string consensus_state = "Idle"; ///< Current state of the consensus engine.
    std::string last_log_message = ""; ///< The last significant log message for display.
};

} // namespace chrono_node
