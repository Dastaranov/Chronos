//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file config.hpp
 * @brief This file defines the Config structure for Chronos node configuration parameters.
 *
 * The Config structure holds various settings that control the behavior of a Chronos node,
 * such as data directory, RPC port, P2P port, and a list of seed peers. It also provides
 * a static method to load configuration from a file.
 *
 * Key functionalities include:
 * - `Config`: A structure to define node configuration parameters.
 * - `load(const std::string& file_path)`: Static method to load configuration from a specified file.
 */

#pragma once

#include <string>
#include <vector> // For std::vector
#include <unordered_map> // For genesis_allocations
#include <cstdint> // For uint64_t

namespace chrono_node {

/**
 * @enum NodeType
 * @brief Defines the operational type of a Chronos node.
 */
enum class NodeType {
    FULL,  ///< A full node stores the entire blockchain and validates all transactions.
    LIGHT  ///< A light node stores only a subset of the blockchain (e.g., headers) and relies on full nodes for validation.
};

/**
 * @struct Config
 * @brief Defines the configuration parameters for a Chronos node.
 *
 * This structure holds various settings that dictate how a Chronos node operates,
 * including paths for data storage, network communication ports, and initial peer connections.
 */
struct Config {
    // Core settings
    NodeType node_type = NodeType::FULL; ///< @var node_type The operational type of this node (Full or Light). Defaults to Full.
    bool is_beacon_node = false; ///< @var is_beacon_node Enables Layer 1 ChronosBeat-only beacon behavior.
    std::string data_dir = "data"; ///< @var data_dir The directory where the node stores its blockchain data. Defaults to "data".
    int rpc_port = 8080; ///< @var rpc_port The port number on which the node's RPC (Remote Procedure Call) server listens. Defaults to 8080.
    std::string rpc_bind_ip = "127.0.0.1"; ///< @var rpc_bind_ip The IP address to bind the RPC server to. Defaults to localhost for security.
    std::string rpc_api_key = ""; ///< @var rpc_api_key Optional API key for RPC authentication.
    
    // Storage settings
    std::string storage_backend = "disk"; // "disk", "leveldb", "memory"
    std::string leveldb_path = "data/leveldb";

    // Network settings
    std::string listen_addr = "0.0.0.0"; ///< @var listen_addr The address the node's network server listens on. Defaults to "0.0.0.0".
    int listen_port = 8645; ///< @var listen_port The port the node's network server listens on. Defaults to 8645.
    std::vector<std::string> network_seeds = {"127.0.0.1:8645"}; ///< @var network_seeds A list of initial network seed addresses.

    // P2P settings
    std::vector<std::string> gossip_topics = {"blocks", "txs", "timeproofs"}; ///< @var gossip_topics Topics for the gossip protocol.
    std::vector<std::string> bootstrap_nodes;
    bool enable_peer_discovery = true;
    int max_peers = 50;
    int min_peers = 10;
    int peer_discovery_interval_ms = 30000;
    int peer_management_interval_ms = 60000;

    // Consensus settings
    int slot_ms = 1000; ///< @var slot_ms Slot duration in milliseconds.
    int bft_round_timeout_ms = 5000; ///< @var bft_round_timeout_ms Timeout for a BFT round in milliseconds.
    double bft_quorum = 0.67; ///< @var bft_quorum BFT quorum threshold.
    std::vector<std::string> validators; ///< @var validators A list of validator node IDs (public keys or addresses).
    // Note: time_weights is complex (map of string to double), will handle in config.cpp if needed.
    double outlier_mad_factor = 6.0; ///< @var outlier_mad_factor MAD factor for outlier detection in PoT.
    double min_threshold_ms = 5.0; ///< @var min_threshold_ms Minimum threshold for PoT timestamps in milliseconds.
    uint64_t pot_epsilon_ms = 1000; ///< @var pot_epsilon_ms Maximum local clock uncertainty epsilon used in PoT timestamp validation.
    uint64_t pot_delta_min_ms = 1; ///< @var pot_delta_min_ms Minimum network latency delta_min used in PoT timestamp validation.
    uint64_t min_stake_nanos = 1000000; ///< @var min_stake_nanos Minimum stake to be a validator.

    // External Time Source settings
    std::vector<std::string> ntp_servers = {"pool.ntp.org", "time.google.com"}; ///< @var ntp_servers List of NTP servers to query.
    long ntp_query_interval_ms = 5000; ///< @var ntp_query_interval_ms Interval in milliseconds for querying NTP servers.
    std::string time_backend = "ntp"; ///< @var time_backend Backend: "ntp", "chrony", "atomic" (future), "quantum" (future).
    std::string atomic_clock_device = "/dev/ppsX"; ///< @var atomic_clock_device Path to atomic clock PPS device (future hardware integration).
    std::string quantum_clock_device = "/dev/quantum0"; ///< @var quantum_clock_device Path to quantum clock device (future hardware integration).

    // Crypto settings
    std::string sign_alg = "dilithium_2"; ///< @var sign_alg Signing algorithm to use.
    std::string prehash = "blake3-256"; ///< @var prehash Pre-hashing algorithm.
    std::string addr_hrp = "cqc"; ///< @var addr_hrp Human-readable part for addresses.
    std::string private_key = ""; ///< @var private_key The secret private key for this node (hex-encoded).
    std::string public_key = ""; ///< @var public_key The public key for this node (hex-encoded).
    std::string private_key_id = ""; ///< @var private_key_id ID for KeyManager lookup.

    // Tokenomics settings
    uint64_t max_total_supply = 31556926;
    uint64_t initial_block_reward_nanos = 0;
    bool minting_enabled = false;
    uint64_t reward_halving_interval = 0;
    uint8_t fee_burn_percentage = 0;
    uint8_t token_decimals = 9;
    uint64_t base_fee_nanos = 1000; ///< @var base_fee_nanos Base transaction fee in nanos.

    // Genesis settings
    std::unordered_map<std::string, uint64_t> genesis_allocations; ///< @var genesis_allocations Initial balance allocations (address -> balance).
    uint64_t genesis_consensus_time = 0; ///< @var genesis_consensus_time Consensus time for genesis block.
    std::string genesis_expected_hash = ""; ///< @var genesis_expected_hash Expected hash of genesis block for validation.
    uint64_t max_supply_per_account = 1000000000000000; ///< @var max_supply_per_account Maximum allowed balance per account (overflow protection).

    /**
     * @brief Validates the configuration parameters.
     * @throws std::invalid_argument if any parameter is invalid.
     */
    void validate() const;

private:
    void validate_network_config() const;
    void validate_consensus_config() const;
    void validate_tokenomics_config() const;
    void validate_crypto_config() const;

public:
    /**
     * @brief Loads configuration parameters from a specified file.
     *
     * This static method reads a configuration file (e.g., TOML, JSON) from the given path
     * and populates a `Config` object with the settings found in the file.
     *
     * @param file_path The path to the configuration file.
     * @return A `Config` object populated with settings from the file.
     */
    static Config load(const std::string& file_path);
};

} // namespace chrono_node