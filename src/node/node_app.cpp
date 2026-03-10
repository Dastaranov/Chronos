//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file node_app.cpp
 * @brief Implements the main application logic for a Chronos node.
 *
 * This file contains the implementation of the `NodeApp` class, which orchestrates all core
 * functionalities of a blockchain node, including initialization, the main event loop,
 * P2P message handling, block and transaction processing, and peer management.
 */
#include <thread>
#include <chrono>
#include <functional>
#include <filesystem>
#include "node/node_app.hpp"
#include "node/node_status.hpp"
#include "util/log.hpp"
#include "util/bytes.hpp" // For hex_to_bytes
#include "util/codec.hpp" // For canonical serialization helpers
#include "storage/IBlockchainStorage.hpp"
#include "storage/DiskBlockchainStorage.hpp" // For default full node storage
#ifdef CHRONOS_USE_LEVELDB
#include "storage/LevelDBBlockchainStorage.hpp"
#endif
#include "storage/file_kv.hpp" // For State's internal IKv
#include "p2p/socket_transport.hpp"
#include "p2p/gossip.hpp"
#include "p2p_messages.pb.h"
#include "bft_messages.pb.h"
#include "consensus/bft_messages.hpp"
#include "rpc/jsonrpc.hpp"
#include "storage/MemoryBlockchainStorage.hpp"
#include "crypto/signer_dilithium.hpp" // New: Include for Dilithium signer
#include "crypto/key_manager.hpp" // For loading keys by ID
#include "consensus/ChronyBackend.hpp"   // For Chrony NTP daemon backend
#include "consensus/AtomicClockBackend.hpp" // Future: hardware atomic clock integration point
#include "consensus/QuantumClockBackend.hpp" // Future: quantum clock integration point

#include "consensus/bft.hpp" // Now needed for make_unique
#include "util/retry_policy.hpp"
#include "util/error_types.hpp"

using namespace chronos;
using namespace chrono_util;
using namespace chrono_ledger;
using namespace chrono_p2p;

namespace chrono_node {

/**
 * @brief Constructs and initializes the NodeApp.
 *
 * Sets up all components of the node based on the provided configuration. This includes
 * initializing the P2P network layer (Gossip), storage (for state and blockchain),
 * consensus mechanisms (PoT and BFT), and the RPC server. It also sets up the
 * message handler for P2P communication.
 *
 * @param cfg The configuration settings for the node.
 */
NodeApp::NodeApp(const chrono_node::Config& cfg)
    : cfg_(cfg), // Store the node's configuration
      status_(), // Initialize node status
      transport_(std::make_unique<chrono_p2p::SocketTransport>(status_)), // Setup P2P socket transport
      gossip_(std::make_shared<chrono_p2p::Gossip>(std::move(transport_))), // Initialize gossip protocol
      peer_store_(std::make_shared<chrono_p2p::PeerStore>(cfg_.data_dir + "/peers.json")), // Initialize PeerStore
      // Initialize cryptographic signer based on configuration
      signer_(nullptr), // Initialize to nullptr first, then assign
      state_kv_store_(std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/chronos_state.kv")), // Setup key-value store for ledger state
      state_(*state_kv_store_), // Initialize ledger state manager
      pot_(cfg_.outlier_mad_factor, cfg_.min_threshold_ms), // Initialize Proof-of-Time aggregator
      bft_(nullptr), // Initialize to nullptr first, then assign after signer_ is ready
      snapshot_manager_(std::make_unique<chrono_storage::SnapshotManager>( // Initialize snapshot manager
                          std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/snapshots.kv"))),
      rpc_(std::make_unique<JsonRpcServer>(cfg_.rpc_port, cfg_.rpc_api_key, state_, *this)), // Initialize JSON-RPC server
      blockchain_storage_(nullptr) // Initialize blockchain storage pointer to nullptr
{
    // Validate tokenomics consistency
    if (cfg_.max_total_supply == 0) {
        throw std::runtime_error("FATAL: max_total_supply cannot be zero");
    }

    // Initialize Signer
    if (!cfg_.private_key.empty()) {
        try {
            chrono_util::Bytes private_key_bytes = chrono_util::hex_to_bytes(cfg_.private_key);
            if (!cfg_.public_key.empty()) {
                chrono_util::Bytes public_key_bytes = chrono_util::hex_to_bytes(cfg_.public_key);
                signer_ = std::make_unique<chrono_crypto::SignerDilithium>(public_key_bytes, private_key_bytes);
            } else {
                signer_ = std::make_unique<chrono_crypto::SignerDilithium>(private_key_bytes);
            }
        } catch (const std::exception& e) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to initialize SignerDilithium from private key: {}", e.what());
            throw std::runtime_error("Failed to load private key.");
        }
    } else if (!cfg_.private_key_id.empty()) {
        try {
            // Assuming keys are stored in a standard location or relative to data_dir
            // For tests, we use data_dir/../keys usually, but here we don't know.
            // Let's assume ~/.chronos/keys or similar.
            // But KeyManager constructor takes a path.
            // We'll use a default path relative to home if possible, or data_dir/keys.
            std::string key_dir = cfg_.data_dir + "/../keys"; // Hack for test structure
            // Better: use a configured key dir. But Config doesn't have it.
            // Fallback to data_dir/keys
            chrono_crypto::KeyManager km(cfg_.data_dir + "/keys"); 
            
            auto key_pair = km.load_key_pair(cfg_.private_key_id);
            if (key_pair) {
                signer_ = std::make_unique<chrono_crypto::SignerDilithium>(key_pair->public_key, key_pair->private_key);
            } else {
                auto priv_key = km.load_private_key(cfg_.private_key_id);
                if (!priv_key.empty()) {
                    signer_ = std::make_unique<chrono_crypto::SignerDilithium>(priv_key);
                } else {
                    throw std::runtime_error("Failed to load key pair for ID: " + cfg_.private_key_id);
                }
            }
        } catch (const std::exception& e) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to initialize SignerDilithium from KeyManager: {}", e.what());
            throw std::runtime_error("Failed to load private key from KeyManager.");
        }
    } else {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Private key is not configured. A validator node cannot run without a private key.");
        throw std::runtime_error("Private key missing in configuration for validator node.");
    }

    // Initialize BFT gadget after signer_ is ready
    // Validator IDs are hex-encoded public keys, matching the config validators list.
    const std::string my_validator_id = chrono_util::bytes_to_hex(signer_->get_public_key());
    bft_ = std::make_unique<chrono_consensus::BftGadget>(
        std::set<std::string>(cfg_.validators.begin(), cfg_.validators.end()), 
        my_validator_id,
        cfg_.bft_quorum,
        cfg_.bft_round_timeout_ms,
        [this]() { return external_time_manager_ ? external_time_manager_->get_current_tier() : 5; });
    
    // Set the signer for BFT message signing
    bft_->set_signer(signer_.get());

    // Set slash callback
    bft_->set_slash_callback([this](const std::string& validator_id, uint64_t penalty, const std::string& reason) {
        state_.slash_validator(validator_id, penalty, reason);
    });


    // Create a lambda function for the TimeMeasurement callback.
    // This callback will be invoked by ExternalTimeSourceManager to pass new time measurements
    // to the PoTAggregator.
    auto measurement_callback = [this](const chrono_consensus::TimeMeasurement& tm) {
        this->pot_.add_timestamp(tm);
    };

    // Initialize the ExternalTimeSourceManager with configured NTP servers and query interval.
    // This manager runs in a background thread to continuously fetch time.
    std::unique_ptr<chrono_consensus::ITimeSyncBackend> time_backend = nullptr;
    if (cfg_.time_backend == "chrony") {
        time_backend = std::make_unique<chrono_consensus::ChronyBackend>();
    } else if (cfg_.time_backend == "atomic") {
        // TODO: Wire real atomic clock hardware (e.g., PPS signal via /dev/ppsX)
        time_backend = std::make_unique<chrono_consensus::AtomicClockBackend>(cfg_.atomic_clock_device);
    } else if (cfg_.time_backend == "quantum") {
        // TODO: Wire real quantum clock hardware when available
        time_backend = std::make_unique<chrono_consensus::QuantumClockBackend>(cfg_.quantum_clock_device);
    }

    external_time_manager_ = std::make_unique<chrono_consensus::ExternalTimeSourceManager>(
        cfg_.ntp_servers, // List of NTP servers from configuration
        cfg_.ntp_query_interval_ms, // Query interval from configuration
        measurement_callback, // Callback to add measurement to PoTAggregator
        std::move(time_backend)
    );

    // Initialize blockchain_storage_ based on node type specified in configuration.
    // Full nodes use DiskBlockchainStorage for persistent storage, Light nodes use MemoryBlockchainStorage.
    if (cfg_.node_type == NodeType::FULL) {
        if (cfg_.storage_backend == "leveldb") {
#ifdef CHRONOS_USE_LEVELDB
            blockchain_storage_ = std::make_unique<chrono_storage::LevelDBBlockchainStorage>(cfg_.leveldb_path);
            LOG_INFO(chrono_util::LogCategory::GENERAL, "Node operating as FULL node. Using LevelDBBlockchainStorage at {}", cfg_.leveldb_path);
#else
            LOG_WARN(chrono_util::LogCategory::GENERAL, "LevelDB requested but not compiled in. Falling back to DiskBlockchainStorage.");
            blockchain_storage_ = std::make_unique<chrono_storage::DiskBlockchainStorage>(
                std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/chronos_blockchain.kv")
            );
            LOG_INFO(chrono_util::LogCategory::GENERAL, "Node operating as FULL node. Using DiskBlockchainStorage.");
#endif
        } else {
            blockchain_storage_ = std::make_unique<chrono_storage::DiskBlockchainStorage>(
                std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/chronos_blockchain.kv")
            );
            LOG_INFO(chrono_util::LogCategory::GENERAL, "Node operating as FULL node. Using DiskBlockchainStorage.");
        }
    } else { // NodeType::LIGHT
        blockchain_storage_ = std::make_unique<chrono_storage::MemoryBlockchainStorage>(
            std::make_unique<chrono_storage::MemoryKv>()
        );
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Node operating as LIGHT node. Using MemoryBlockchainStorage.");
    }

    LOG_INFO(chrono_util::LogCategory::GENERAL, "NodeApp initialized with data_dir: {}", cfg_.data_dir);

    // Ensure data directory exists (create all parent directories if needed)
    std::error_code ec;
    std::filesystem::create_directories(cfg_.data_dir, ec);
    if (ec) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Could not create data_dir '{}': {}", cfg_.data_dir, ec.message());
    }

    // Set up node status identifiers and addresses based on configuration.
    status_.node_id = "ChronosNode-" + std::to_string(cfg_.listen_port);
    status_.rpc_address = "127.0.0.1:" + std::to_string(cfg_.rpc_port);
    status_.p2p_address = "127.0.0.1:" + std::to_string(cfg_.listen_port);

    // Set the message handler for incoming P2P messages using the gossip protocol.
    gossip_->set_message_handler(std::bind(&NodeApp::handle_p2p_message, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

    // Initialize Discovery Manager
    peer_store_->load_from_disk();
    discovery_manager_ = std::make_shared<chrono_p2p::DiscoveryManager>(
        peer_store_,
        gossip_,
        cfg_.bootstrap_nodes,
        cfg_.max_peers,
        cfg_.min_peers,
        cfg_.peer_discovery_interval_ms
    );
    
    if (cfg_.enable_peer_discovery) {
        discovery_manager_->start();
    }
    
    // Initialize timers from config
    peer_discovery_interval_ms_ = std::chrono::milliseconds(cfg_.peer_discovery_interval_ms);
    peer_management_interval_ms_ = std::chrono::milliseconds(cfg_.peer_management_interval_ms);
}

/**
 * @brief Destructor for NodeApp.
 * Ensures that unique_ptr members are properly destroyed and background threads are stopped.
 */
NodeApp::~NodeApp() {
    // Ensure all background operations are gracefully shut down.
    if (discovery_manager_) {
        discovery_manager_->stop();
    }
    // Stop the ExternalTimeSourceManager's worker thread to prevent further time queries.
    if (external_time_manager_) {
        external_time_manager_->stop();
    }
    // Other unique_ptr members (transport_, gossip_, signer_, etc.) are automatically
    // destroyed when NodeApp is destruct destructed, handling their cleanup.
}


/**
 * @brief Starts and runs the main event loop of the node.
 *
 * This function initializes networking, starts the RPC server, and enters the main
 * consensus and peer management loop.
 */
void NodeApp::run() {
    LOG_INFO(chrono_util::LogCategory::GENERAL, "NodeApp running...");
    running_ = true;

    uint64_t loaded_next_block_height = 0;
    chrono_util::Bytes loaded_last_block_hash;

    // Load blockchain state from persistence or create genesis block
    auto height_data = blockchain_storage_->getMetadata(NEXT_BLOCK_HEIGHT_KEY);
    if (height_data) {
        // Read height as uint64_t LE
        size_t offset = 0;
        loaded_next_block_height = chrono_util::read_fixed_uint64_le(*height_data, offset);
        auto hash_data = blockchain_storage_->getMetadata(LAST_BLOCK_HASH_KEY);
        if (hash_data) {
            loaded_last_block_hash = *hash_data;
        }
        // Update after loading to reflect correct values
        {
            std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
            last_block_hash_ = loaded_last_block_hash;
            next_block_height_ = loaded_next_block_height;
        }
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Loaded blockchain state: height = {}, last_hash = {}", loaded_next_block_height, bytes_to_hex(loaded_last_block_hash));
        
        // Load genesis hash
        auto genesis_blk = blockchain_storage_->getBlock(0);
        if (genesis_blk) {
            genesis_block_hash_ = genesis_blk->get_header_hash();
            
            // Validate against expected hash if configured
            if (!cfg_.genesis_expected_hash.empty()) {
                chrono_util::Bytes expected_hash = chrono_util::hex_to_bytes(cfg_.genesis_expected_hash);
                if (expected_hash != genesis_block_hash_) {
                    std::string error_msg = "FATAL: Loaded genesis block hash mismatch! Expected: " + 
                                           chrono_util::bytes_to_hex(expected_hash) + 
                                           ", Got: " + chrono_util::bytes_to_hex(genesis_block_hash_);
                    LOG_ERROR(chrono_util::LogCategory::CONSENSUS, error_msg);
                    throw std::runtime_error(error_msg);
                }
            }
        } else {
             LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Could not load genesis block (height 0). Genesis hash validation in handshake might fail.");
        }
    } else {
        // First run: create and store a genesis block
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "No blockchain state found. Creating genesis block...");
        loaded_last_block_hash = Bytes(32, 0); // Zero hash for previous block of genesis

        // Use configured genesis consensus time (or 0 if not configured)
        uint64_t genesis_consensus_time = cfg_.genesis_consensus_time;
        uint32_t genesis_time_tier = 1; // Genesis block is assumed to be Tier 1 (Quantum/Trusted)
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Configured genesis consensus time: {}", genesis_consensus_time);
        
        if (genesis_consensus_time == 0) {
            // Fallback to current PoT consensus time if not configured
            try {
                genesis_consensus_time = pot_.get_consensus_time();
                genesis_time_tier = external_time_manager_->get_current_tier();
                LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Using current consensus time {} (Tier {}) for genesis block", genesis_consensus_time, genesis_time_tier);
            } catch (const std::exception& e) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, "No PoT measurements for genesis time. Using system time.");
                genesis_consensus_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()
                ).count();
                genesis_time_tier = 5; // System time is Tier 5
            }
        }

        Block genesis_block(loaded_last_block_hash, 0, genesis_consensus_time, 0, genesis_time_tier, 100, {});
        genesis_block.timestamp = genesis_consensus_time; // Force deterministic timestamp for genesis
        
        // Validate against expected hash if configured
        if (!cfg_.genesis_expected_hash.empty()) {
            chrono_util::Bytes expected_hash = chrono_util::hex_to_bytes(cfg_.genesis_expected_hash);
            chrono_util::Bytes actual_hash = genesis_block.get_header_hash();
            
            if (expected_hash != actual_hash) {
                std::string error_msg = "Genesis block hash mismatch! Expected: " + 
                                       chrono_util::bytes_to_hex(expected_hash) + 
                                       ", Got: " + chrono_util::bytes_to_hex(actual_hash);
                LOG_ERROR(chrono_util::LogCategory::CONSENSUS, error_msg);
                throw std::runtime_error(error_msg);
            }
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Genesis block hash validated successfully");
        }
        
        // Apply genesis allocations to state
        for (const auto& [address_str, balance] : cfg_.genesis_allocations) {
            // Validate address format
            if (!chrono_address::Address::is_valid(address_str)) {
                LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Invalid genesis allocation address: {}", address_str);
                throw std::runtime_error("Invalid genesis allocation address: " + address_str);
            }
            
            // Validate balance doesn't exceed max supply per account
            if (balance > cfg_.max_supply_per_account) {
                LOG_ERROR(chrono_util::LogCategory::CONSENSUS, 
                         "Genesis allocation for {} exceeds max_supply_per_account: {} > {}", 
                         address_str, balance, cfg_.max_supply_per_account);
                throw std::runtime_error("Genesis allocation exceeds max supply for address: " + address_str);
            }
            
            // Set initial balance
            state_.set_balance(address_str, balance);
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Genesis allocation: {} = {} nanos", address_str, balance);
        }
        
        add_block(genesis_block); // This will update last_block_hash_ and next_block_height_
        {
            std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
            last_block_hash_ = genesis_block.get_header_hash();
            next_block_height_ = 1; // Genesis block height 0, next block height 1
        }
        genesis_block_hash_ = genesis_block.get_header_hash();
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Genesis block created with hash: {}", bytes_to_hex(genesis_block.get_header_hash()));
        
        // Sync BFT gadget to next height (1)
        bft_->advance_to_next_height(next_block_height_);
    }

    // Attempt to start P2P listening on the configured address and port. Log an error and exit if it fails.
    if (!gossip_->start_listening(cfg_.listen_addr, cfg_.listen_port)) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to start P2P listening on {}:{}", cfg_.listen_addr, cfg_.listen_port);
        return;
    }

    // Iterate through configured seed peers and attempt to establish connections.
    // For each seed peer, parse its host and port, then connect via the gossip layer.
    for (const auto& peer : cfg_.network_seeds) {
        size_t colon_pos = peer.find(':');
        if (colon_pos != std::string::npos) {
            std::string host = peer.substr(0, colon_pos);
            int port = std::stoi(peer.substr(colon_pos + 1));
            if (gossip_->connect_to_peer(host, port)) {
                LOG_INFO(chrono_util::LogCategory::GENERAL, "Connected to seed peer: {}", peer);
                send_handshake(peer); // Send initial handshake message to the newly connected peer.
            } else {
                LOG_WARN(chrono_util::LogCategory::GENERAL, "Failed to connect to seed peer: {}", peer);
            }
        }
    }

    // Start the RPC server in a detached background thread.
    // The RPC server handles incoming client requests.
    std::thread rpc_thread([this]() {
        if (rpc_) {
            rpc_->serve();
        } else {
            LOG_ERROR(chrono_util::LogCategory::GENERAL, "RPC server not initialized.");
        }
    });
    rpc_thread.detach();

    // Start the ExternalTimeSourceManager to begin periodically fetching time measurements
    // from external NTP sources. These measurements will be fed into the PoTAggregator.
    external_time_manager_->start();

    // Initialize BFT state to match the loaded blockchain state.
    uint64_t current_bft_height = next_block_height_;
    uint32_t current_bft_round = 0;

    // Initialize timers for periodic tasks
    last_peer_discovery_time_ = std::chrono::steady_clock::now();
    last_peer_management_time_ = std::chrono::steady_clock::now();
    last_snapshot_discovery_time_ = std::chrono::steady_clock::now();
    last_consensus_progress_ = std::chrono::steady_clock::now();

    // Main event loop
    while (running_) {
        // Sleep for one slot duration (configurable via slot_ms in config).
        // This is the block production rate; keep it well below the network RTT for multi-node setups.
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.slot_ms));

        // --- Dynamic Peer Discovery ---
        auto now = std::chrono::steady_clock::now();
        
        // Detect stuck consensus
        if (now - last_consensus_progress_ > std::chrono::seconds(30)) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                     "Consensus appears stuck at height {}, round {}. Forcing new round.",
                     next_block_height_, bft_->get_current_round());
            
            // Force new round
            bft_->force_new_round("timeout_recovery");
            last_consensus_progress_ = now;
        }

        if (now - last_peer_discovery_time_ >= peer_discovery_interval_ms_) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Initiating peer discovery.");
            P2PMessage peer_list_envelope;
            auto* peer_list_msg = peer_list_envelope.mutable_get_peers_response();
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                for (const auto& pair : connected_peers_) {
                    auto* peer_addr = peer_list_msg->add_peers();
                    peer_addr->set_address(pair.second.address);
                    peer_addr->set_node_id(pair.first);
                    // peer_addr->set_last_seen(...);
                }
            }
            gossip_->publish("peer_list", peer_list_envelope);
            last_peer_discovery_time_ = now;
        }

        // --- Peer Management ---
        if (now - last_peer_management_time_ >= peer_management_interval_ms_) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Initiating peer management.");
            manage_peers();
            
            // Retry connecting to seeds if we have few peers
            if (status_.connected_peers < cfg_.min_peers) {
                for (const auto& peer : cfg_.network_seeds) {
                    size_t colon_pos = peer.find(':');
                    if (colon_pos != std::string::npos) {
                        std::string host = peer.substr(0, colon_pos);
                        int port = std::stoi(peer.substr(colon_pos + 1));
                        // Check if already connected? Gossip handles duplicate connections usually
                        if (gossip_->connect_to_peer(host, port)) {
                            send_handshake(peer);
                        }
                    }
                }
            }

            if (peer_store_) {
                peer_store_->save_to_disk();
            }
            last_peer_management_time_ = now;
        }

        // --- Block Synchronization Management ---
        // Continuously check if we need to sync blocks from peers
        // and manage active sync sessions (timeouts, peer switching, etc.)
        manage_sync();

        // --- Snapshot Discovery ---
        if (now - last_snapshot_discovery_time_ >= snapshot_discovery_interval_ms_) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Initiating snapshot discovery.");
            P2PMessage get_snapshots_envelope;
            auto* get_snapshots_msg = get_snapshots_envelope.mutable_get_snapshots();
            uint64_t current_height = 0;
            {
                std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
                current_height = next_block_height_;
            }
            get_snapshots_msg->set_min_height(current_height); // Request snapshots newer than our current chain
            get_snapshots_msg->set_max_height(current_height + 1000); // Or some reasonable max height
            gossip_->publish("get_snapshots", get_snapshots_envelope);
            last_snapshot_discovery_time_ = now;
        }

        // --- Consensus Logic ---
        // Timestamps are now added to pot_ by ExternalTimeSourceManager
        uint64_t current_consensus_time = 0;
        try {
            current_consensus_time = pot_.get_consensus_time();
        } catch (const std::exception& e) {
            // If PoT is not ready, we can't participate in consensus yet.
            // Log warning and skip this round tick.
            // LOG_WARN(chrono_util::LogCategory::CONSENSUS, "PoT not ready: {}", e.what());
            continue;
        }
        
        current_bft_height = bft_->get_current_height(); // Get actual height from BFT gadget
        current_bft_round = bft_->get_current_round();   // Get actual round from BFT gadget
        
        // Update status for display
        status_.consensus_round = current_bft_round;
        status_.current_block_height = next_block_height_ - 1;
        status_.mempool_size = mempool_.size();
        {
             std::lock_guard<std::mutex> lock(peers_mutex_);
             status_.connected_peers = connected_peers_.size();
        }
        // Update pending votes count
        status_.pending_votes = state_.get_node_registry().get_pending_approval_count();

        // Use 0 for consensus_time to ensure deterministic leader selection matching BftGadget::handle_new_round
        std::string current_leader = bft_->get_leader_for_round(0, current_bft_height, current_bft_round);
        const std::string my_validator_id = chrono_util::bytes_to_hex(signer_->get_public_key());

        // Leader logic
        if (current_leader == my_validator_id) {
            // Check if we already proposed for this round (BftGadget will track this)
            if (!bft_->has_proposed_for_round(current_bft_height, current_bft_round)) {
                std::vector<chrono_ledger::Transaction> transactions_for_block;
                chrono_util::Bytes last_block_for_proposal;
                {
                    std::lock_guard<std::mutex> lock_mempool(mempool_mutex_);
                    // Use select_transactions_for_block to filter and sort by fee
                    auto [selected_txs, collected_fees] = select_transactions_for_block(mempool_);
                    transactions_for_block = selected_txs;
                    
                    // If transactions were selected, collect leader rewards
                    if (collected_fees > 0) {
                        std::lock_guard<std::mutex> lock_state(blockchain_state_mutex_);
                        state_.credit(signer_->get_address(), collected_fees);
                        LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                                 "Leader collected {} in transaction fees for height {} round {}.",
                                 collected_fees, current_bft_height, current_bft_round);
                    }
                    
                    // Clear mempool after selection
                    mempool_.clear();
                }
                {
                    std::lock_guard<std::mutex> lock_state(blockchain_state_mutex_);
                    last_block_for_proposal = last_block_hash_;
                }

                // Leaders always propose a block, even when the mempool is empty.
                // This keeps the chain advancing and prevents consensus from stalling.
                LOG_INFO(chrono_util::LogCategory::CONSENSUS, "[{}] I am the leader for height {} round {}. Proposing block with {} transactions.",
                         status_.node_id, current_bft_height, current_bft_round, transactions_for_block.size());

                // Get current time tier and score
                uint32_t current_tier = external_time_manager_->get_current_tier();
                uint32_t current_score = static_cast<uint32_t>(external_time_manager_->get_time_quality_score());

                // Create a new block
                Block new_block(last_block_for_proposal, current_bft_height, current_consensus_time, current_bft_round, current_tier, current_score, transactions_for_block);
                
                // Create a Protobuf NewRound message
                chronos::bft::NewRound new_round_msg_proto;
                new_round_msg_proto.set_height(current_bft_height);
                new_round_msg_proto.set_round(current_bft_round);
                new_round_msg_proto.set_proposal_block_hash(new_block.get_header_hash().data(), new_block.get_header_hash().size());
                new_round_msg_proto.set_validator_id(my_validator_id);
                new_round_msg_proto.set_time_tier(current_tier);
                
                chrono_util::Bytes message_hash = compute_bft_message_hash(
                    current_bft_height,
                    current_bft_round,
                    new_block.get_header_hash(),
                    my_validator_id,
                    current_tier
                );
                chrono_util::Bytes signed_data = signer_->sign_message(message_hash);
                new_round_msg_proto.mutable_signature()->set_data(signed_data.data(), signed_data.size());

                    // Inform BFT gadget that this node has proposed a block
                    bft_->set_proposed_block(new_block);

                    // Process this new round message internally, which will trigger this node to prevote for its own block
                    std::optional<chronos::bft::Prevote> self_prevote = bft_->handle_new_round(new_round_msg_proto);

                    // Broadcast NewRound message to peers so they can start the round
                    P2PMessage new_round_envelope;
                    *new_round_envelope.mutable_new_round() = new_round_msg_proto;
                    gossip_->publish("new_round", new_round_envelope);
                    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Published NewRound message for height {} round {}", current_bft_height, current_bft_round);

                    if (self_prevote) {
                        P2PMessage prevote_envelope;
                        auto* prevote_proto = prevote_envelope.mutable_prevote();
                        prevote_proto->set_height(self_prevote->height());
                        prevote_proto->set_round(self_prevote->round());
                        prevote_proto->set_block_hash(self_prevote->block_hash().data(), self_prevote->block_hash().size());
                        prevote_proto->set_validator_id(self_prevote->validator_id());
                        prevote_proto->set_time_tier(self_prevote->time_tier());
                        prevote_proto->mutable_signature()->set_data(self_prevote->signature().data().c_str(), self_prevote->signature().data().size());
                        gossip_->publish("prevote", prevote_envelope);
                        LOG_INFO(chrono_util::LogCategory::P2P, "Published self-generated Prevote for height {} round {}", self_prevote->height(), self_prevote->round());
                        
                        // Add own vote to BFT gadget; handle precommit if quorum reached
                        if (auto precommit = bft_->handle_prevote(*self_prevote)) {
                            P2PMessage precommit_envelope;
                            auto* precommit_proto = precommit_envelope.mutable_precommit();
                            precommit_proto->set_height(precommit->height());
                            precommit_proto->set_round(precommit->round());
                            precommit_proto->set_block_hash(precommit->block_hash().data(), precommit->block_hash().size());
                            precommit_proto->set_validator_id(precommit->validator_id());
                            precommit_proto->set_time_tier(precommit->time_tier());
                            precommit_proto->mutable_signature()->set_data(precommit->signature().data().c_str(), precommit->signature().data().size());
                            gossip_->publish("precommit", precommit_envelope);
                            LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Broadcasted Precommit for height {} round {}",
                                     precommit->height(), precommit->round());

                            // Handle own precommit — finalize block if quorum reached
                            if (auto finalized_block = bft_->handle_precommit(*precommit)) {
                                LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Finalizing block {} at height {}",
                                         bytes_to_hex(finalized_block->get_header_hash()), finalized_block->height);
                                add_block(*finalized_block);
                            }
                        }
                    }

                    // Publish the block itself for followers to validate
                    P2PMessage block_envelope;
                    block_envelope.mutable_block()->set_block_data(new_block.serialize().data(), new_block.serialize().size());
                    broadcast_message("blocks", block_envelope);
                    LOG_INFO(chrono_util::LogCategory::P2P, "Published new proposed block {} to network.", bytes_to_hex(new_block.get_header_hash()));

            } else {
                 LOG_INFO(chrono_util::LogCategory::CONSENSUS, "I am the leader for height {} round {} and already proposed. Waiting.",
                             current_bft_height, current_bft_round);
            }
        } else {
            // Follower logic: handled by handle_p2p_message
            LOG_DEBUG(chrono_util::LogCategory::CONSENSUS, "I am a follower. Leader is {}. Waiting for messages.", current_leader);
        }

        // --- Timeout Handling ---
        // Check if the current round has timed out without reaching consensus.
        // This prevents the network from getting stuck indefinitely in a single round.
        if (bft_->is_round_timed_out()) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                     "Round timeout detected for height {} round {}. Advancing to next round.",
                     current_bft_height, current_bft_round);
            
            // Advance to next round and get NewRound message
            chronos::bft::NewRound new_round_msg = bft_->advance_to_next_round();
            
            // Broadcast NewRound message to all validators
            // (In production, this would go through gossip to inform other validators)
            // For now, we just log the advancement. In a real network, other validators
            // would receive and process this message to stay synchronized.
            
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                     "Advanced to round {} after timeout. Waiting for new proposal.",
                     bft_->get_current_round());

        }
    }
}

void NodeApp::stop() {
    LOG_INFO(chrono_util::LogCategory::GENERAL, "NodeApp stopping...");
    running_ = false;
}

/**
 * @brief Validates and adds a block to the blockchain.
 *
 * This function performs critical validation before adding a block to the chain:
 * 1.  It checks if the block's consensus time is within an acceptable tolerance of the local node's time.
 * 2.  It submits the block to the BFT gadget for finalization.
 *
 * If the block is valid and finalized, its transactions are applied to the state, the block is
 * persisted (for full nodes), and the node's internal state (height, last block hash) is updated.
 * It also triggers periodic snapshot creation.
 *
 * @param b The block to be added.
 * @return True if the block was successfully validated and added, false otherwise.
 */
bool NodeApp::add_block(const Block& b) {
    // Validate block integrity and consensus rules
    if (!b.is_valid()) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Block {} validation failed. Rejecting.", bytes_to_hex(b.get_header_hash()));
        return false;
    }

    // PoT Consensus Time Validation
    // Skip for genesis block (height 0) as we might not have measurements yet
    if (b.height > 0) {
        try {
            uint64_t current_local_consensus_time = pot_.get_consensus_time();
            const uint64_t CONSENSUS_TIME_TOLERANCE_MS = 10000; 

            if (std::abs(static_cast<int64_t>(b.consensus_time) - static_cast<int64_t>(current_local_consensus_time)) > CONSENSUS_TIME_TOLERANCE_MS) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Block {} has consensus_time {} which is outside acceptable tolerance of local consensus_time {}. Rejecting block.",
                         bytes_to_hex(b.get_header_hash()), b.consensus_time, current_local_consensus_time);
                return false;
            }
        } catch (const std::exception& e) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Could not validate block time (no local measurements): {}", e.what());
            // Allow to proceed if we can't validate time yet (e.g. early network)
        }
    }

    // Message-driven BFT: Check if we have a precommit quorum for this block.
    // In a fully message-driven design, BFT handlers (handle_prevote, handle_precommit)
    // accumulate messages and signal when quorum is reached. The add_block() method
    // should only be called after sufficient consensus messages have been processed.
    if (!bft_->check_precommit_quorum(next_block_height_, bft_->get_current_round())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Block {} does not have a precommit quorum. Not finalizing yet.", bytes_to_hex(b.get_header_hash()));
        return false;
    }

    // Verify that the finalized block hash matches the proposed block
    auto finalized_hash = bft_->get_finalized_block_hash();
    if (!finalized_hash || *finalized_hash != b.get_header_hash()) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Block {} does not match finalized block hash from BFT.", bytes_to_hex(b.get_header_hash()));
        return false;
    }

    uint64_t total_fees = 0;
    for (const auto& tx : b.transactions) {
        if (!state_.apply_transaction(tx)) {
            LOG_WARN(chrono_util::LogCategory::GENERAL, "Failed to apply transaction {} from block {}: Insufficient funds or invalid transaction.", tx.to_string(), bytes_to_hex(b.get_header_hash()));
            // In a real system, this could invalidate the entire block.
        } else {
            total_fees += tx.fee;
        }
    }

    // Apply Block Reward and Fees
    uint64_t reward = calculate_block_reward(b.height);
    std::string leader_address = bft_->get_leader_for_round(b.consensus_time, b.height, b.round);
    
    uint64_t burned_fees = 0;
    if (cfg_.fee_burn_percentage > 0) {
        burned_fees = (total_fees * cfg_.fee_burn_percentage) / 100;
    }
    uint64_t validator_fees = total_fees - burned_fees;
    
    uint64_t total_credit = reward + validator_fees;

    if (total_credit > 0) {
        state_.credit(leader_address, total_credit);
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                 "Credited {} (Reward: {}, Fees: {}) to leader {}. Burned Fees: {}", 
                 total_credit, reward, validator_fees, leader_address, burned_fees);
    }

    // Persist the block only for full nodes
    if (cfg_.node_type == NodeType::FULL) {
        auto save_result = chrono_error::retry_with_backoff<bool>(
            [this, &b]() -> chrono_error::Result<bool> {
                if (blockchain_storage_->saveBlock(b)) {
                    return chrono_error::Result<bool>(true);
                } else {
                    return chrono_error::Result<bool>(
                        chrono_error::StorageErrorCode::IOError,
                        "Failed to save block"
                    );
                }
            },
            3, 100
        );

        if (!save_result.is_success()) {
            LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to save block {} after retries: {}", 
                      bytes_to_hex(b.get_header_hash()), save_result.error_message());
            // In a real system, we might want to halt or degrade here.
            // For now, we log the critical error.
        }
    }

    // Update and persist blockchain state metadata
    uint64_t new_height = b.height + 1;
    Bytes height_bytes;
    chrono_util::write_fixed_uint64_le(new_height, height_bytes);
    blockchain_storage_->saveMetadata(NEXT_BLOCK_HEIGHT_KEY, height_bytes);
    blockchain_storage_->saveMetadata(LAST_BLOCK_HASH_KEY, b.get_header_hash());
    
    // Update consensus progress timer
    last_consensus_progress_ = std::chrono::steady_clock::now();

    // Create snapshot periodically for full nodes
    if (cfg_.node_type == NodeType::FULL) {
        const uint64_t SNAPSHOT_INTERVAL_BLOCKS = 10;
        if (b.height > 0 && b.height % SNAPSHOT_INTERVAL_BLOCKS == 0) {
            LOG_INFO(chrono_util::LogCategory::STORAGE, "Attempting to create snapshot at height {}", b.height);
            if (!snapshot_manager_->createSnapshot(b.height, state_, b.get_header_hash())) {
                LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to create snapshot at height {}", b.height);
            }
        }
    }
    
    // Update in-memory state (thread-safe)
    {
        std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
        last_block_hash_ = b.get_header_hash();
        next_block_height_ = new_height;
    }

    // Clean up mempool after block finalization (remove applied transactions)
    // Note: select_transactions_for_block already clears mempool during proposal,
    // but ensure it's clean for any leftover transactions from other proposals
    {
        std::lock_guard<std::mutex> lock(mempool_mutex_);
        // Remove transactions that were included in this block
        mempool_.erase(
            std::remove_if(mempool_.begin(), mempool_.end(),
                          [&b](const Transaction& tx) {
                              // Check if this tx is in the block
                              return std::find_if(b.transactions.begin(), b.transactions.end(),
                                                [&tx](const Transaction& block_tx) {
                                                    return block_tx.get_hash_for_signing() == tx.get_hash_for_signing();
                                                }) != b.transactions.end();
                          }),
            mempool_.end());
    }

    // Advance BFT state for next consensus round
    bft_->advance_to_next_height(next_block_height_);

    // Update validators based on new state
    auto active_validators = state_.get_node_registry().get_active_validators(cfg_.min_stake_nanos);
    status_.active_validators = active_validators.size(); // Update status
    std::set<std::string> validator_ids;
    for (const auto& v : active_validators) {
        validator_ids.insert(v.node_id);
    }
    // If no validators registered (e.g. early network), fallback to config validators
    if (validator_ids.empty()) {
        validator_ids.insert(cfg_.validators.begin(), cfg_.validators.end());
    }
    bft_->update_validators(validator_ids);

    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Advanced BFT to next height: {}", next_block_height_);

    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Block {} finalized and added to ledger. New height: {}", bytes_to_hex(b.get_header_hash()), next_block_height_);
    return true;
}

/**
 * @brief Adds a transaction from an external source (e.g., RPC) to the mempool.
 *
 * @param tx The transaction to add.
 */
void NodeApp::add_transaction(const Transaction& tx) {
    if (add_transaction_to_mempool(tx)) {
      LOG_INFO(chrono_util::LogCategory::GENERAL, "Manually added transaction to mempool: {}", tx.to_string());
      publish_transaction(tx); // Broadcast to network
    }
}

/**
 * @brief Validates and adds a transaction to the memory pool.
 *
 * Performs comprehensive validation before accepting transaction into mempool:
 * 1. Basic validity check (is_valid() - checks signature/pubkey presence)
 * 2. Recipient address validation (valid Bech32m format)
 * 3. Amount validation (no zero-amount transactions)
 * 4. Fee validation (must meet MIN_FEE requirement)
 * 5. Signature verification (cryptographic authenticity)
 * 6. Nonce validation (replay attack prevention)
 * 7. Duplicate detection (prevent same tx from entering mempool twice)
 * 8. Balance validation (sender has sufficient funds for amount + fee)
 *    - Includes overflow check to prevent uint64_t wrapping
 *
 * @param tx The transaction to add.
 * @return True if the transaction passed all validations and was added, false otherwise.
 */
bool NodeApp::add_transaction_to_mempool(const chrono_ledger::Transaction& tx) {
    // 0. Mempool size limit check
    {
        std::lock_guard<std::mutex> lock(mempool_mutex_);
        if (mempool_.size() >= MAX_MEMPOOL_SIZE) {
            LOG_WARN(chrono_util::LogCategory::GENERAL, "Rejected transaction (mempool full): {}", tx.to_string());
            return false;
        }
    }

    // 1. Basic validity check
    if (!tx.is_valid()) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "[{}] Rejected transaction (invalid format): {}", status_.node_id, tx.to_string());
        return false;
    }

    std::string sender_addr = tx.sender.to_string();
    std::string recipient_addr = tx.recipient.to_string();

    // 2. Recipient address validation
    if (!chrono_address::Address::is_valid(recipient_addr)) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "[{}] Rejected transaction (invalid recipient address): {}", status_.node_id, tx.to_string());
        return false;
    }

    // 3. Amount validation - zero amount transactions not allowed
    if (tx.amount == 0) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "[{}] Rejected transaction (zero amount) from {}: {}", status_.node_id, sender_addr, tx.to_string());
        return false;
    }

    // 4. Fee validation - minimum fee requirement
    if (tx.fee < MIN_FEE) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "[{}] Rejected transaction (fee too low) from {}: fee={}, minimum={}",
                 status_.node_id, sender_addr, tx.fee, MIN_FEE);
        return false;
    }

    // 5. Signature verification
    Bytes hash_to_verify = tx.get_hash_for_signing();
    if (!signer_->verify(tx.public_key, hash_to_verify, tx.signature)) {
        std::cout << "DEBUG: NodeApp Verify PubKey: " << chrono_util::bytes_to_hex(tx.public_key) << std::endl;
        std::cout << "DEBUG: NodeApp Tx JSON: " << tx.to_string() << std::endl;
        LOG_WARN(chrono_util::LogCategory::GENERAL, "[{}] Rejected transaction (invalid signature) from {}: {}", status_.node_id, sender_addr, tx.to_string());
        LOG_WARN(chrono_util::LogCategory::GENERAL, "[{}] Verify Hash: {}", status_.node_id, chrono_util::bytes_to_hex(hash_to_verify));
        LOG_WARN(chrono_util::LogCategory::GENERAL, "[{}] Verify PubKey: {}", status_.node_id, chrono_util::bytes_to_hex(tx.public_key));
        update_peer_score("local", -5, true);  // Penalize self for bad transaction
        return false;
    }

    // 5b. Public Key Validation (Address match)
    // Ensure the public key provided matches the sender address
    chrono_address::Address derived_address(tx.public_key);
    if (derived_address != tx.sender) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "[{}] Rejected transaction (public key mismatch) from {}: derived={}, expected={}", 
                 status_.node_id, sender_addr, derived_address.to_string(), sender_addr);
        return false;
    }

    // 6. Nonce validation - transaction nonce must match expected next nonce for sender
    // We check both the committed state and the mempool to allow chaining transactions
    uint64_t expected_nonce = state_.get_nonce(sender_addr);
    
    {
        std::lock_guard<std::mutex> lock(mempool_mutex_);
        for (const auto& pending_tx : mempool_) {
            // We use address string comparison or operator== if available
            if (pending_tx.sender.to_string() == sender_addr) {
                if (pending_tx.nonce >= expected_nonce) {
                    expected_nonce = pending_tx.nonce + 1;
                }
            }
        }
        LOG_DEBUG(chrono_util::LogCategory::GENERAL, "[{}] Nonce check for {}: state={}, expected_after_mempool={}, tx={}", 
                  status_.node_id, sender_addr, state_.get_nonce(sender_addr), expected_nonce, tx.nonce);
    }

    if (tx.nonce != expected_nonce) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "[{}] Rejected transaction (nonce mismatch) from {}: expected {}, got {}",
                 status_.node_id, sender_addr, expected_nonce, tx.nonce);
        return false;
    }

    // 7. Duplicate detection - check if this exact transaction is already in mempool
    {
        std::lock_guard<std::mutex> lock(mempool_mutex_);
        Bytes tx_hash = tx.get_hash_for_signing();
        for (const auto& existing_tx : mempool_) {
            if (existing_tx.get_hash_for_signing() == tx_hash) {
                LOG_WARN(chrono_util::LogCategory::GENERAL, "[{}] Rejected transaction (duplicate): {}", status_.node_id, tx.to_string());
                return false;
            }
        }
    }

    // 8. Balance validation - sender must have sufficient funds for amount + fee
    // Check for overflow first to prevent wrapping
    uint64_t sender_balance = state_.get_balance(sender_addr);
    if (tx.amount > UINT64_MAX - tx.fee) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "Rejected transaction (overflow) from {}: amount={}, fee={}",
                 sender_addr, tx.amount, tx.fee);
        return false;
    }
    uint64_t total_cost = tx.amount + tx.fee;
    if (sender_balance < total_cost) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "Rejected transaction (insufficient balance) from {}: balance={}, needed={}",
                 sender_addr, sender_balance, total_cost);
        return false;
    }

    // 9. All validations passed - add to mempool (thread-safe)
    {
        std::lock_guard<std::mutex> lock(mempool_mutex_);
        mempool_.push_back(tx);
    }
    LOG_INFO(chrono_util::LogCategory::GENERAL, "[{}] Added transaction to mempool: {}", status_.node_id, tx.to_string());
    return true;
}

/**
 * @brief Broadcasts a transaction to the network.
 *
 * Serializes the transaction and publishes it on the 'transactions' topic using the gossip protocol.
 *
 * @param tx The transaction to publish.
 */
void NodeApp::publish_transaction(const chrono_ledger::Transaction& tx) {
    P2PMessage tx_envelope;
    tx_envelope.mutable_transaction()->set_transaction_data(tx.serialize().data(), tx.serialize().size());
    broadcast_message("transactions", tx_envelope);
    LOG_INFO(chrono_util::LogCategory::P2P, "Published transaction {} to network.", tx.to_string());
}

/**
 * @brief Selects and prepares transactions for block proposal by the leader.
 *
 * Filters mempool to select valid transactions with sufficient fees, sorted by fee (highest first).
 * Applies transactions sequentially and validates state. Rolls back state on transaction failure.
 * Collects leader rewards from transaction fees.
 *
 * @param mempool The current mempool containing candidate transactions.
 * @return A pair of (selected transactions vector, total fees collected).
 */
std::pair<std::vector<chrono_ledger::Transaction>, uint64_t> NodeApp::select_transactions_for_block(
    const std::vector<chrono_ledger::Transaction>& mempool) {
    
    std::vector<chrono_ledger::Transaction> selected_txs;
    uint64_t total_fees = 0;
    
    // Create a copy and sort by fee (highest first) for greedy selection
    auto sortable_txs = mempool;
    std::sort(sortable_txs.begin(), sortable_txs.end(),
              [](const chrono_ledger::Transaction& a, const chrono_ledger::Transaction& b) {
                  return a.fee > b.fee;  // Higher fee comes first
              });
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "[{}] Selecting transactions for block. Mempool has {} txs.", status_.node_id, sortable_txs.size());
    
    // Try to apply each transaction in fee order
    for (const auto& tx : sortable_txs) {
        // Validate minimum fee requirement
        if (tx.fee < MIN_FEE) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                     "Skipping transaction with insufficient fee: {} < {}", tx.fee, MIN_FEE);
            continue;
        }
        
        // Validate transaction one more time before adding to block
        if (!tx.is_valid()) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                     "Skipping invalid transaction: {}", tx.to_string());
            continue;
        }
        
        // Try to apply transaction to state
        try {
            state_.apply_transaction(tx);
            selected_txs.push_back(tx);
            total_fees += tx.fee;
            
            LOG_DEBUG(chrono_util::LogCategory::CONSENSUS, 
                      "Selected transaction with fee {}: {}", tx.fee, tx.to_string());
        } catch (const std::exception& e) {
            // Transaction application failed - skip it and continue with next
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                     "Skipping transaction (application failed): {} - Error: {}", 
                     tx.to_string(), e.what());
            // Note: State changes from failed transaction are already rolled back by apply_transaction
            continue;
        }
    }
    
    if (!selected_txs.empty()) {
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                 "Selected {} transactions for block, collecting {} in fees.",
                 selected_txs.size(), total_fees);
    }
    
    return {selected_txs, total_fees};
}

/**
 * @brief Main handler for all incoming P2P messages from the gossip network.
 *
 * This function is the central dispatcher for network messages. It deserializes the incoming
 * message, identifies its type (Handshake, Block, Transaction, BFT messages, etc.),
 * and routes it to the appropriate specific handler function. It also implements a basic
 * peer scoring mechanism, rewarding valid messages and penalizing invalid ones.
 *
 * @param topic The gossip topic on which the message was received.
 * @param data The raw byte payload of the message.
 * @param sender_id The unique identifier of the sending peer.
 */
void NodeApp::handle_p2p_message(const std::string& topic, const Bytes& data, const std::string& sender_id) { 
    update_peer_score(sender_id, 1); 

    // VALIDATION 1: Check message size before parsing
    if (data.size() > MAX_MESSAGE_SIZE) {
        LOG_ERROR(chrono_util::LogCategory::P2P, 
                  "Rejecting oversized message from {}: {} bytes (max: {})", 
                  sender_id, data.size(), MAX_MESSAGE_SIZE);
        update_peer_score(sender_id, -10, true); // Severe penalty for DoS attempt
        return;
    }

    if (data.empty()) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Rejecting empty message from {}", sender_id);
        update_peer_score(sender_id, -5, true);
        return;
    }

    chrono_p2p::P2PMessage p2p_msg;
    if (!p2p_msg.ParseFromArray(data.data(), data.size())) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to parse Protobuf P2PMessage from incoming data from {}.", sender_id);
        update_peer_score(sender_id, -5, true);
        return;
    }

    // Handle Fragmentation
    if (p2p_msg.payload_case() == chrono_p2p::P2PMessage::kFragment) {
        auto reassembled_msg = fragmentation_manager_.handle_fragment(p2p_msg.fragment());
        if (reassembled_msg) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Reassembled fragmented message from {}. Processing.", sender_id);
            // Recursively process the reassembled message
            // We serialize it back to bytes to reuse the existing flow (simplest integration)
            std::string reassembled_data = reassembled_msg->SerializeAsString();
            handle_p2p_message(topic, chrono_util::Bytes(reassembled_data.begin(), reassembled_data.end()), sender_id);
        }
        return; // Done with this fragment
    }

    switch (p2p_msg.payload_case()) {
        case chrono_p2p::P2PMessage::kHandshake: {
            const auto& received_handshake = p2p_msg.handshake();
            
            // VALIDATION 2: Validate handshake message fields
            if (!validate_handshake_message(received_handshake, sender_id)) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Invalid handshake message from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -5, true);
                break;
            }
            
            LOG_INFO(chrono_util::LogCategory::P2P, "Received Handshake from Node ID: {}, P2P Port: {}, Block Height: {}, Last Block Hash: {}, Time Tier: {}, Score: {}",
                     received_handshake.node_id(), received_handshake.port(),
                     received_handshake.current_block_height(), bytes_to_hex(Bytes(received_handshake.last_block_hash().begin(), received_handshake.last_block_hash().end())),
                     received_handshake.time_tier(), received_handshake.time_quality_score());

            PeerInfo peer_info;
            peer_info.node_id = received_handshake.node_id();
            peer_info.address = received_handshake.node_id() + ":" + std::to_string(received_handshake.port());
            peer_info.last_block_hash = Bytes(received_handshake.last_block_hash().begin(), received_handshake.last_block_hash().end());
            peer_info.current_block_height = received_handshake.current_block_height();
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                if (connected_peers_.find(received_handshake.node_id()) == connected_peers_.end()) {
                     // New peer, send handshake back
                     // We use the address from the handshake or the sender_id if mapped
                     // Since we don't have a direct send_handshake(node_id) yet, we rely on publish
                     // or we can trigger a handshake broadcast.
                     // Better: just call send_handshake with the address from the message
                     // But send_handshake takes address string just for logging?
                     // Actually send_handshake uses gossip_->publish("handshake", ...).
                     // This broadcasts to all. This is inefficient but should work for now.
                     // To avoid infinite loops, we only send if we haven't added them yet.
                     // But we are inside the lock, so we add them first?
                     // No, we add them below.
                     
                     // Let's add them first, then send handshake.
                     connected_peers_[received_handshake.node_id()] = peer_info;
                     
                     // Send handshake back
                     // We need to release lock before calling send_handshake to avoid potential deadlocks if it locks something
                } else {
                     connected_peers_[received_handshake.node_id()] = peer_info;
                }
            }
            
            // Send handshake back to ensure bidirectional connection
            // We do this every time we receive a handshake to be safe, or only if new?
            // If we do it every time, we might loop if both sides do it.
            // But we only do it if we didn't know them?
            // Let's rely on the fact that if we initiated the connection, we sent handshake first.
            // If they initiated, they sent handshake. We need to reply.
            // How do we know who initiated?
            // We don't easily.
            // But if we are receiving a handshake, and we haven't sent one recently...
            
            // Simple fix: If we just added them (new connection), send a handshake.
            // But I can't easily check "just added" outside the lock without a flag.
            
            bool is_new_peer = false;
            {
                 std::lock_guard<std::mutex> lock(peers_mutex_);
                 // Check if we already knew them? 
                 // Wait, I just added them above.
                 // Let's refactor.
            }
            
            // Actually, send_handshake broadcasts.
            // If I receive a handshake, and I broadcast a handshake, the other peer receives it.
            // If they already know me, they process it (update info).
            // If they don't, they add me and broadcast handshake?
            // Then I receive it... Loop!
            
            // We need to avoid the loop.
            // Only send handshake if we initiated the connection?
            // Or if we are responding to an incoming connection?
            
            // Standard P2P:
            // A connects to B.
            // A sends Handshake.
            // B receives Handshake. B adds A. B sends Handshake.
            // A receives Handshake. A adds B. A does NOT send Handshake (already did).
            
            // How does A know not to send?
            // A sent it immediately after connect.
            // So when A receives B's handshake, A already considers B "handshaked" or "connected"?
            // Our `connected_peers_` map is the source of truth.
            // If A adds B to `connected_peers_` BEFORE sending handshake?
            // No, A sends handshake, then waits for B's handshake?
            // Or A sends handshake, B receives, B adds A, B sends handshake.
            // A receives, A adds B.
            
            // If A receives B's handshake, and A already has B in `connected_peers_`, A does nothing.
            // If A does NOT have B, A adds B and sends Handshake.
            
            // So:
            bool known = false;
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                if (connected_peers_.count(received_handshake.node_id())) {
                    known = true;
                    connected_peers_[received_handshake.node_id()] = peer_info; // Update info
                } else {
                    connected_peers_[received_handshake.node_id()] = peer_info; // Add new
                }
            }
            
            if (!known) {
                send_handshake(peer_info.address, received_handshake.node_id());
            }
            
            // Synchronization logic: If peer has a longer chain, request blocks.
            uint64_t current_height = 0;
            chrono_util::Bytes current_block_hash;
            {
                std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
                current_height = next_block_height_;
                current_block_hash = last_block_hash_;
            }

            if (received_handshake.current_block_height() > (current_height - 1)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Peer {} has a longer chain ({} vs local {}). Initiating block synchronization.",
                         received_handshake.node_id(), received_handshake.current_block_height(), (current_height - 1));
                
                P2PMessage get_blocks_envelope;
                auto* get_blocks_msg = get_blocks_envelope.mutable_get_blocks();
                get_blocks_msg->set_from_block_hash(current_block_hash.data(), current_block_hash.size());
                get_blocks_msg->set_limit(10); // Request a batch of blocks

                gossip_->publish("get_blocks", get_blocks_envelope);
                LOG_INFO(chrono_util::LogCategory::P2P, "Requested blocks from peer {} starting from {}.", 
                         received_handshake.node_id(), bytes_to_hex(current_block_hash));

            } else if (received_handshake.current_block_height() < (current_height - 1)) {
                 LOG_INFO(chrono_util::LogCategory::P2P, "Local node has a longer chain ({} vs peer {}). Peer might need sync.",
                         (current_height - 1), received_handshake.current_block_height());
            } else {
                 LOG_INFO(chrono_util::LogCategory::P2P, "Local chain height matches peer ({}). No sync needed for height.", (current_height - 1));
            }
            break;
        }
        case chrono_p2p::P2PMessage::kGetBlocks: {
            const auto& get_blocks_req = p2p_msg.get_blocks();
            
            // VALIDATION 3: Validate GetBlocks message
            if (!validate_get_blocks_message(get_blocks_req, sender_id)) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Invalid GetBlocks message from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -5, true);
                break;
            }
            
            LOG_INFO(chrono_util::LogCategory::P2P, "Received GetBlocks request from_block_hash: {}, limit: {}",
                     bytes_to_hex(Bytes(get_blocks_req.from_block_hash().begin(), get_blocks_req.from_block_hash().end())), get_blocks_req.limit());

            std::optional<Block> block = blockchain_storage_->getBlock(Bytes(get_blocks_req.from_block_hash().begin(), get_blocks_req.from_block_hash().end()));
            if (block) {
                P2PMessage block_envelope;
                block_envelope.mutable_block()->set_block_data(block->serialize().data(), block->serialize().size());
                gossip_->publish("blocks", block_envelope);
                LOG_INFO(chrono_util::LogCategory::P2P, "Sent block {} in response to GetBlocks request.", bytes_to_hex(block->get_header_hash()));
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "Requested block {} not found. Penalizing sender {}.", bytes_to_hex(Bytes(get_blocks_req.from_block_hash().begin(), get_blocks_req.from_block_hash().end())), sender_id);
                update_peer_score(sender_id, -1, true); // Penalize for requesting non-existent block
            }
            break;
        }
        case chrono_p2p::P2PMessage::kBlock: {
            const auto& received_block_proto = p2p_msg.block();
            
            // VALIDATION 7: Check block data size
            if (received_block_proto.block_data().size() > MAX_BLOCK_SIZE) {
                LOG_WARN(chrono_util::LogCategory::P2P,
                         "Rejecting oversized block from {}: {} bytes (max: {})",
                         sender_id, received_block_proto.block_data().size(), MAX_BLOCK_SIZE);
                update_peer_score(sender_id, -10, true);
                break;
            }
            
            if (received_block_proto.block_data().empty()) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Rejecting empty block from {}", sender_id);
                update_peer_score(sender_id, -10, true);
                break;
            }
            
            // Deserialize with try-catch for malformed data
            Block received_block;
            try {
                received_block = Block::deserialize(Bytes(received_block_proto.block_data().begin(), 
                                                         received_block_proto.block_data().end()));
            } catch (const std::exception& e) {
                LOG_WARN(chrono_util::LogCategory::P2P,
                         "Failed to deserialize block from {}: {}. Penalizing.",
                         sender_id, e.what());
                update_peer_score(sender_id, -10, true);
                break;
            }
            
            // Check for potential fork before adding block
            chrono_util::Bytes received_block_hash = received_block.get_header_hash();
            uint64_t received_block_height = received_block.height;  // Direct member access
            
            detect_and_resolve_fork(received_block_hash, received_block_height, sender_id);
            
            // Validate block integrity
            if (!received_block.is_valid()) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Received invalid block from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -10, true);
                break;
            }

            // Check if this is a proposal for current round and vote if appropriate
            if (auto prevote = bft_->on_block_received(received_block)) {
                 P2PMessage prevote_envelope;
                 auto* prevote_proto = prevote_envelope.mutable_prevote();
                 prevote_proto->set_height(prevote->height());
                 prevote_proto->set_round(prevote->round());
                 prevote_proto->set_block_hash(prevote->block_hash().data(), prevote->block_hash().size());
                 prevote_proto->set_validator_id(prevote->validator_id());
                 prevote_proto->set_time_tier(prevote->time_tier());
                 prevote_proto->mutable_signature()->set_data(prevote->signature().data().c_str(), prevote->signature().data().size());
                 gossip_->publish("prevote", prevote_envelope);
                 LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Broadcasted Prevote for block {} height {} round {}", 
                          bytes_to_hex(received_block.get_header_hash()), prevote->height(), prevote->round());
                 
                 // Add own vote to BFT gadget
                 if (auto precommit = bft_->handle_prevote(*prevote)) {
                     // Broadcast precommit
                     P2PMessage precommit_envelope;
                     auto* precommit_proto = precommit_envelope.mutable_precommit();
                     precommit_proto->set_height(precommit->height());
                     precommit_proto->set_round(precommit->round());
                     precommit_proto->set_block_hash(precommit->block_hash().data(), precommit->block_hash().size());
                     precommit_proto->set_validator_id(precommit->validator_id());
                     precommit_proto->set_time_tier(precommit->time_tier());
                     precommit_proto->mutable_signature()->set_data(precommit->signature().data().c_str(), precommit->signature().data().size());
                     gossip_->publish("precommit", precommit_envelope);
                     LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Broadcasted Precommit for block {} height {} round {}", 
                              bytes_to_hex(received_block.get_header_hash()), precommit->height(), precommit->round());
                     
                     // Handle own precommit
                     if (auto finalized_block = bft_->handle_precommit(*precommit)) {
                         // Finalize block
                         LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Finalizing block {} after own precommit", bytes_to_hex(finalized_block->get_header_hash()));
                         add_block(*finalized_block);
                     }
                 }
            } else {
                 // Check if it's a future block and buffer it
                 uint64_t current_h = bft_->get_current_height();
                 uint32_t current_r = bft_->get_current_round();
                 
                 if (received_block.height > current_h || 
                    (received_block.height == current_h && received_block.round > current_r)) {
                     LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Buffering future block {} for height {} round {}", 
                              bytes_to_hex(received_block.get_header_hash()), received_block.height, received_block.round);
                     std::lock_guard<std::mutex> lock(pending_blocks_mutex_);
                     pending_blocks_[{received_block.height, received_block.round}] = received_block;
                 }
            }

            if (add_block(received_block)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Received and added block with hash: {}", bytes_to_hex(received_block.get_header_hash()));
                
                // Track sync progress if we're currently syncing
                if (is_syncing_) {
                    sync_downloaded_blocks_++;
                    last_sync_progress_time_ = std::chrono::steady_clock::now();
                    
                    uint64_t current_height;
                    {
                        std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
                        current_height = next_block_height_ - 1;
                    }
                    
                    // Request next batch if we haven't reached target yet
                    if (current_height < sync_target_height_) {
                        chrono_util::Bytes current_block_hash;
                        {
                            std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
                            current_block_hash = last_block_hash_;
                        }
                        
                        chrono_p2p::P2PMessage get_blocks_envelope;
                        auto* get_blocks_msg = get_blocks_envelope.mutable_get_blocks();
                        get_blocks_msg->set_from_block_hash(current_block_hash.data(), current_block_hash.size());
                        get_blocks_msg->set_limit(SYNC_BATCH_SIZE);
                        
                        gossip_->publish("get_blocks", get_blocks_envelope);
                        
                        LOG_INFO(chrono_util::LogCategory::P2P, 
                                 "Sync progress: {} / {} blocks. Requesting next batch.",
                                 sync_downloaded_blocks_, (sync_target_height_ - (current_height - sync_downloaded_blocks_)));
                    }
                }
            } else {
                // Block valid but not finalized yet (waiting for quorum)
                LOG_DEBUG(chrono_util::LogCategory::P2P, "Received block {} from {} but not finalized yet.", bytes_to_hex(received_block.get_header_hash()), sender_id);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kTransaction: {
            const auto& received_tx_proto = p2p_msg.transaction();
            
            // VALIDATION 8: Check transaction data size
            if (received_tx_proto.transaction_data().size() > MAX_TRANSACTION_SIZE) {
                LOG_WARN(chrono_util::LogCategory::P2P,
                         "Rejecting oversized transaction from {}: {} bytes (max: {})",
                         sender_id, received_tx_proto.transaction_data().size(), MAX_TRANSACTION_SIZE);
                update_peer_score(sender_id, -5, true);
                break;
            }
            
            if (received_tx_proto.transaction_data().empty()) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Rejecting empty transaction from {}", sender_id);
                update_peer_score(sender_id, -5, true);
                break;
            }
            
            // Deserialize with try-catch for malformed data
            Transaction received_tx;
            try {
                received_tx = Transaction::deserialize(Bytes(received_tx_proto.transaction_data().begin(), 
                                                            received_tx_proto.transaction_data().end()));
            } catch (const std::exception& e) {
                LOG_WARN(chrono_util::LogCategory::P2P,
                         "Failed to deserialize transaction from {}: {}. Penalizing.",
                         sender_id, e.what());
                update_peer_score(sender_id, -5, true);
                break;
            }
            
            if (add_transaction_to_mempool(received_tx)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Received and added transaction to mempool: {}", received_tx.to_string());
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "Failed to add transaction {} received from {}. Penalizing sender.", received_tx.to_string(), sender_id);
                update_peer_score(sender_id, -1, true);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kPrevote: {
            const auto& received_prevote_proto = p2p_msg.prevote();
            
            // VALIDATION 4: Validate BFT message fields
            chrono_util::Bytes block_hash_bytes(received_prevote_proto.block_hash().begin(), 
                                               received_prevote_proto.block_hash().end());
            if (!validate_bft_message(received_prevote_proto.height(), 
                                     received_prevote_proto.round(),
                                     block_hash_bytes,
                                     received_prevote_proto.validator_id(),
                                     "Prevote", sender_id)) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Invalid Prevote message from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -10, true);
                break;
            }
            
            LOG_INFO(chrono_util::LogCategory::P2P, "Received BFT Prevote from validator: {}", received_prevote_proto.validator_id());
            
            // STEP 1: Verify signature before processing message
            chrono_util::Bytes message_hash = compute_bft_message_hash(
                received_prevote_proto.height(),
                received_prevote_proto.round(),
                block_hash_bytes,
                received_prevote_proto.validator_id(),
                received_prevote_proto.time_tier()
            );
            
            chrono_util::Bytes signature_bytes(received_prevote_proto.signature().data().begin(), 
                                               received_prevote_proto.signature().data().end());
            
            if (!verify_bft_message_signature(received_prevote_proto.validator_id(), message_hash, signature_bytes)) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                         "Prevote signature verification failed from validator {}. Rejecting message and penalizing peer.",
                         received_prevote_proto.validator_id());
                update_peer_score(sender_id, -10, true); // Significant penalty for invalid signature
                break;
            }
            
            // STEP 2: Process message if signature is valid
            if(auto precommit_to_send = bft_->handle_prevote(received_prevote_proto)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Sending BFT Precommit after prevote quorum for height {} round {}", precommit_to_send->height(), precommit_to_send->round());
                P2PMessage precommit_envelope;
                auto* precommit_proto = precommit_envelope.mutable_precommit();
                precommit_proto->set_height(precommit_to_send->height());
                precommit_proto->set_round(precommit_to_send->round());
                precommit_proto->set_block_hash(precommit_to_send->block_hash().data(), precommit_to_send->block_hash().size());
                precommit_proto->set_validator_id(precommit_to_send->validator_id());
                precommit_proto->set_time_tier(precommit_to_send->time_tier());
                precommit_proto->mutable_signature()->set_data(precommit_to_send->signature().data().c_str(), precommit_to_send->signature().data().size());
                gossip_->publish("precommit", precommit_envelope);
                
                // Add own precommit to BFT gadget
                if (auto finalized_block = bft_->handle_precommit(*precommit_to_send)) {
                     LOG_INFO(chrono_util::LogCategory::P2P, "Finalizing block after precommit quorum (self): {}", bytes_to_hex(finalized_block->get_header_hash()));
                     if (!add_block(*finalized_block)) {
                         LOG_WARN(chrono_util::LogCategory::P2P, "Failed to add finalized block (self) {}. ", bytes_to_hex(finalized_block->get_header_hash()));
                     }
                }
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "BFT gadget rejected Prevote from {}. Penalizing sender.", sender_id);
                update_peer_score(sender_id, -5, true);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kPrecommit: {
            const auto& received_precommit_proto = p2p_msg.precommit();
            
            // VALIDATION 5: Validate BFT message fields
            chrono_util::Bytes block_hash_bytes(received_precommit_proto.block_hash().begin(), 
                                               received_precommit_proto.block_hash().end());
            if (!validate_bft_message(received_precommit_proto.height(), 
                                     received_precommit_proto.round(),
                                     block_hash_bytes,
                                     received_precommit_proto.validator_id(),
                                     "Precommit", sender_id)) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Invalid Precommit message from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -10, true);
                break;
            }
            
            LOG_INFO(chrono_util::LogCategory::P2P, "Received BFT Precommit from validator: {}", received_precommit_proto.validator_id());
            
            // STEP 1: Verify signature before processing message
            chrono_util::Bytes message_hash = compute_bft_message_hash(
                received_precommit_proto.height(),
                received_precommit_proto.round(),
                block_hash_bytes,
                received_precommit_proto.validator_id(),
                received_precommit_proto.time_tier()
            );
            
            chrono_util::Bytes signature_bytes(received_precommit_proto.signature().data().begin(), 
                                               received_precommit_proto.signature().data().end());
            
            if (!verify_bft_message_signature(received_precommit_proto.validator_id(), message_hash, signature_bytes)) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                         "Precommit signature verification failed from validator {}. Rejecting message and penalizing peer.",
                         received_precommit_proto.validator_id());
                update_peer_score(sender_id, -10, true); // Significant penalty for invalid signature
                break;
            }
            
            // STEP 2: Process message if signature is valid
            if (auto block_to_finalize = bft_->handle_precommit(received_precommit_proto)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Finalizing block after precommit quorum: {}", bytes_to_hex(block_to_finalize->get_header_hash()));
                if (!add_block(*block_to_finalize)) {
                    LOG_WARN(chrono_util::LogCategory::P2P, "Failed to add finalized block {} from {}. Penalizing sender.", bytes_to_hex(block_to_finalize->get_header_hash()), sender_id);
                    update_peer_score(sender_id, -5, true);
                }
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "BFT gadget rejected Precommit from {}. Penalizing sender.", sender_id);
                update_peer_score(sender_id, -5, true);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kNewRound: {
            const auto& received_new_round_proto = p2p_msg.new_round();
            
            // VALIDATION 6: Validate BFT message fields
            chrono_util::Bytes proposal_hash_bytes(received_new_round_proto.proposal_block_hash().begin(), 
                                                   received_new_round_proto.proposal_block_hash().end());
            if (!validate_bft_message(received_new_round_proto.height(), 
                                     received_new_round_proto.round(),
                                     proposal_hash_bytes,
                                     received_new_round_proto.validator_id(),
                                     "NewRound", sender_id)) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Invalid NewRound message from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -10, true);
                break;
            }
            
            LOG_INFO(chrono_util::LogCategory::P2P, "Received BFT NewRound from validator: {}. Height: {}, Round: {}",
                     received_new_round_proto.validator_id(), received_new_round_proto.height(), received_new_round_proto.round());
            
            // STEP 1: Verify signature before processing message
            chrono_util::Bytes message_hash = compute_bft_message_hash(
                received_new_round_proto.height(),
                received_new_round_proto.round(),
                proposal_hash_bytes,
                received_new_round_proto.validator_id(),
                received_new_round_proto.time_tier()
            );
            
            chrono_util::Bytes signature_bytes(received_new_round_proto.signature().data().begin(), 
                                               received_new_round_proto.signature().data().end());
            
            if (!verify_bft_message_signature(received_new_round_proto.validator_id(), message_hash, signature_bytes)) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                         "NewRound signature verification failed from validator {}. Rejecting message and penalizing peer.",
                         received_new_round_proto.validator_id());
                update_peer_score(sender_id, -10, true); // Significant penalty for invalid signature
                break;
            }
            
            // STEP 2: Process message if signature is valid
            if (auto prevote_to_send = bft_->handle_new_round(received_new_round_proto)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Sending BFT Prevote after NewRound message for height {} round {}",
                         prevote_to_send->height(), prevote_to_send->round());
                P2PMessage prevote_envelope;
                auto* prevote_proto = prevote_envelope.mutable_prevote();
                prevote_proto->set_height(prevote_to_send->height());
                prevote_proto->set_round(prevote_to_send->round());
                prevote_proto->set_block_hash(prevote_to_send->block_hash().data(), prevote_to_send->block_hash().size());
                prevote_proto->set_validator_id(prevote_to_send->validator_id());
                prevote_proto->set_time_tier(prevote_to_send->time_tier());
                prevote_proto->mutable_signature()->set_data(prevote_to_send->signature().data().c_str(), prevote_to_send->signature().data().size());
                gossip_->publish("prevote", prevote_envelope);
                
                // Add own vote to BFT gadget
                if (auto precommit = bft_->handle_prevote(*prevote_to_send)) {
                    // Broadcast precommit
                    P2PMessage precommit_envelope;
                    auto* precommit_proto = precommit_envelope.mutable_precommit();
                    precommit_proto->set_height(precommit->height());
                    precommit_proto->set_round(precommit->round());
                    precommit_proto->set_block_hash(precommit->block_hash().data(), precommit->block_hash().size());
                    precommit_proto->set_validator_id(precommit->validator_id());
                    precommit_proto->set_time_tier(precommit->time_tier());
                    precommit_proto->mutable_signature()->set_data(precommit->signature().data().c_str(), precommit->signature().data().size());
                    gossip_->publish("precommit", precommit_envelope);
                    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Broadcasted Precommit for height {} round {}", 
                             precommit->height(), precommit->round());
                    
                    // Handle own precommit
                    if (auto finalized_block = bft_->handle_precommit(*precommit)) {
                        // Finalize block
                        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Finalizing block {} after own precommit", bytes_to_hex(finalized_block->get_header_hash()));
                        add_block(*finalized_block);
                    }
                }
            } else {
                // Check pending blocks
                uint64_t h = received_new_round_proto.height();
                uint32_t r = received_new_round_proto.round();
                
                std::lock_guard<std::mutex> lock(pending_blocks_mutex_);
                auto it = pending_blocks_.find({h, r});
                if (it != pending_blocks_.end()) {
                    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Found buffered block for height {} round {}", h, r);
                    if (auto prevote = bft_->on_block_received(it->second)) {
                        P2PMessage prevote_envelope;
                        auto* prevote_proto = prevote_envelope.mutable_prevote();
                        prevote_proto->set_height(prevote->height());
                        prevote_proto->set_round(prevote->round());
                        prevote_proto->set_block_hash(prevote->block_hash().data(), prevote->block_hash().size());
                        prevote_proto->set_validator_id(prevote->validator_id());
                        prevote_proto->set_time_tier(prevote->time_tier());
                        prevote_proto->mutable_signature()->set_data(prevote->signature().data().c_str(), prevote->signature().data().size());
                        gossip_->publish("prevote", prevote_envelope);
                        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Broadcasted Prevote for buffered block {} height {} round {}", 
                                 prevote->block_hash(), prevote->height(), prevote->round());
                        
                        // Add own vote to BFT gadget
                        if (auto precommit = bft_->handle_prevote(*prevote)) {
                            // Broadcast precommit
                            P2PMessage precommit_envelope;
                            auto* precommit_proto = precommit_envelope.mutable_precommit();
                            precommit_proto->set_height(precommit->height());
                            precommit_proto->set_round(precommit->round());
                            precommit_proto->set_block_hash(precommit->block_hash().data(), precommit->block_hash().size());
                            precommit_proto->set_validator_id(precommit->validator_id());
                            precommit_proto->set_time_tier(precommit->time_tier());
                            precommit_proto->mutable_signature()->set_data(precommit->signature().data().c_str(), precommit->signature().data().size());
                            gossip_->publish("precommit", precommit_envelope);
                            LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Broadcasted Precommit for buffered block {} height {} round {}", 
                                     bytes_to_hex(Bytes(prevote->block_hash().begin(), prevote->block_hash().end())), precommit->height(), precommit->round());
                            
                            // Handle own precommit
                            if (auto finalized_block = bft_->handle_precommit(*precommit)) {
                                // Finalize block
                                LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Finalizing block {} after own precommit", bytes_to_hex(finalized_block->get_header_hash()));
                                add_block(*finalized_block);
                            }
                        }
                    }
                    pending_blocks_.erase(it);
                } else {
                    LOG_WARN(chrono_util::LogCategory::P2P, "BFT gadget rejected NewRound from {}. Penalizing sender.", sender_id);
                    update_peer_score(sender_id, -5, true);
                }
            }
            break;
        }
        // Duplicate case removed - merged logic below
        /*
        case chrono_p2p::P2PMessage::kGetPeersResponse: {
            const auto& peer_list_proto = p2p_msg.get_peers_response();
            // ... old logic ...
            break;
        }
        */
        case chrono_p2p::P2PMessage::kGetPeersRequest: {
            const auto& req = p2p_msg.get_peers_request();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received GetPeersRequest from {}", sender_id);
            if (discovery_manager_) {
                discovery_manager_->handle_get_peers_request(sender_id, req.max_peers());
            }
            break;
        }
        case chrono_p2p::P2PMessage::kGetPeersResponse: {
            const auto& resp = p2p_msg.get_peers_response();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received GetPeersResponse from {} with {} peers", 
                     sender_id, resp.peers_size());
            if (discovery_manager_) {
                std::vector<chrono_p2p::PeerAddress> peers;
                for (const auto& p : resp.peers()) {
                    peers.push_back(p);
                }
                discovery_manager_->handle_get_peers_response(peers);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kGetSnapshots: {
            const auto& get_snapshots_req = p2p_msg.get_snapshots();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received GetSnapshots request from {} for height range {}-{}.",
                     sender_id, get_snapshots_req.min_height(), get_snapshots_req.max_height());
            
            P2PMessage snapshots_available_envelope;
            auto* snapshots_available_msg = snapshots_available_envelope.mutable_snapshots_available();
            
            // In a real system, you'd query snapshot_manager_ for available snapshots.
            // This is a simplified check for the latest snapshot.
            std::optional<chrono_storage::SnapshotData> available_snapshot = snapshot_manager_->restoreSnapshot(next_block_height_ - 1); 
            if (available_snapshot && available_snapshot->height >= get_snapshots_req.min_height() && available_snapshot->height <= get_snapshots_req.max_height()) {
                auto* metadata = snapshots_available_msg->add_available_snapshots();
                metadata->set_height(available_snapshot->height);
                metadata->set_last_block_hash(available_snapshot->last_block_hash.data(), available_snapshot->last_block_hash.size());
                gossip_->publish("snapshots_available", snapshots_available_envelope);
                LOG_INFO(chrono_util::LogCategory::P2P, "Responded to GetSnapshots from {} with snapshot at height {}.", sender_id, available_snapshot->height);
            } else {
                LOG_INFO(chrono_util::LogCategory::P2P, "No suitable snapshot found for GetSnapshots request from {}.", sender_id);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kSnapshotsAvailable: {
            const auto& snapshots_available_msg = p2p_msg.snapshots_available();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received SnapshotsAvailable message from {}.", sender_id);
            for (const auto& snapshot_meta : snapshots_available_msg.available_snapshots()) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Peer {} has snapshot at height {}, last block {}.",
                         sender_id, snapshot_meta.height(), bytes_to_hex(Bytes(snapshot_meta.last_block_hash().begin(), snapshot_meta.last_block_hash().end())));
                
                // If peer has a newer snapshot, and we are far behind, request it.
                if (snapshot_meta.height() > next_block_height_ + 100) { // If peer's snapshot is significantly newer
                    LOG_INFO(chrono_util::LogCategory::P2P, "Peer {} has significantly newer snapshot at height {}. Requesting chunks.", sender_id, snapshot_meta.height());
                    
                    // Initialize download state
                    snapshot_download_state_ = SnapshotDownloadState{snapshot_meta.height(), {}, 0};

                    P2PMessage get_chunk_envelope;
                    auto* get_chunk_msg = get_chunk_envelope.mutable_get_snapshot_chunk();
                    get_chunk_msg->set_snapshot_height(snapshot_meta.height());
                    get_chunk_msg->set_chunk_index(0); // Request first chunk
                    get_chunk_msg->set_chunk_size(1024 * 1024); // Request 1MB chunks
                    gossip_->publish("get_snapshot_chunk", get_chunk_envelope);
                    
                    // Only request one snapshot at a time
                    break;
                }
            }
            break;
        }
        case chrono_p2p::P2PMessage::kGetSnapshotChunk: {
            const auto& get_chunk_req = p2p_msg.get_snapshot_chunk();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received GetSnapshotChunk request from {} for height {} chunk {}.",
                     sender_id, get_chunk_req.snapshot_height(), get_chunk_req.chunk_index());
            
            chrono_util::Bytes chunk_data = snapshot_manager_->getSnapshotChunk(
                get_chunk_req.snapshot_height(), 
                get_chunk_req.chunk_index(), 
                get_chunk_req.chunk_size());
            
            if (!chunk_data.empty()) {
                P2PMessage chunk_envelope;
                auto* chunk_msg = chunk_envelope.mutable_snapshot_chunk();
                chunk_msg->set_snapshot_height(get_chunk_req.snapshot_height());
                chunk_msg->set_chunk_index(get_chunk_req.chunk_index());
                chunk_msg->set_chunk_data(chunk_data.data(), chunk_data.size());
                
                // Check if this is the last chunk
                uint64_t total_size = snapshot_manager_->getSnapshotSize(get_chunk_req.snapshot_height());
                bool is_last = ((get_chunk_req.chunk_index() + 1) * get_chunk_req.chunk_size()) >= total_size;
                chunk_msg->set_is_last_chunk(is_last);
                
                broadcast_message("snapshot_chunk", chunk_envelope);
                LOG_INFO(chrono_util::LogCategory::P2P, "Sent snapshot chunk {} for height {} to {}.", get_chunk_req.chunk_index(), get_chunk_req.snapshot_height(), sender_id);
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "Requested snapshot chunk not found or empty for height {}.", get_chunk_req.snapshot_height());
            }
            break;
        }
        case chrono_p2p::P2PMessage::kSnapshotChunk: {
            const auto& chunk_msg = p2p_msg.snapshot_chunk();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received SnapshotChunk for height {} chunk {} (last: {}). Size: {}",
                     chunk_msg.snapshot_height(), chunk_msg.chunk_index(), chunk_msg.is_last_chunk(), chunk_msg.chunk_data().size());
            
            if (!snapshot_download_state_ || snapshot_download_state_->height != chunk_msg.snapshot_height()) {
                // Initialize new download if not started or different height
                snapshot_download_state_ = SnapshotDownloadState{chunk_msg.snapshot_height(), {}, 0};
            }

            if (chunk_msg.chunk_index() != snapshot_download_state_->next_chunk_index) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Received out-of-order snapshot chunk. Expected {}, got {}. Ignoring.", 
                         snapshot_download_state_->next_chunk_index, chunk_msg.chunk_index());
                break;
            }

            // Append data
            snapshot_download_state_->data.insert(
                snapshot_download_state_->data.end(), 
                chunk_msg.chunk_data().begin(), 
                chunk_msg.chunk_data().end());
            snapshot_download_state_->next_chunk_index++;

            if (chunk_msg.is_last_chunk()) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Snapshot reassembly complete for height {}. Restoring state...", chunk_msg.snapshot_height());
                
                auto restored_data = snapshot_manager_->restoreFromBytes(snapshot_download_state_->data);
                if (restored_data) {
                    start_from_snapshot(*restored_data);
                    snapshot_download_state_.reset(); // Clear download state
                } else {
                    LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to restore state from downloaded snapshot chunks.");
                    snapshot_download_state_.reset(); // Clear corrupted state
                }
            } else {
                // Request next chunk
                P2PMessage get_chunk_envelope;
                auto* get_chunk_msg = get_chunk_envelope.mutable_get_snapshot_chunk();
                get_chunk_msg->set_snapshot_height(chunk_msg.snapshot_height());
                get_chunk_msg->set_chunk_index(snapshot_download_state_->next_chunk_index);
                get_chunk_msg->set_chunk_size(1024 * 1024); // 1MB chunks
                gossip_->publish("get_snapshot_chunk", get_chunk_envelope);
            }
            break;
        }
        case chrono_p2p::P2PMessage::PAYLOAD_NOT_SET: {
            LOG_WARN(chrono_util::LogCategory::P2P, "Received P2P message with unknown payload type from {}.", sender_id);
            update_peer_score(sender_id, -1, true);
            break;
        }
    }
} // Closes handle_p2p_message

/**
 * @brief Sends a handshake message to a peer.
 *
 * This message shares the node's identity, P2P port, and current blockchain state
 * (height and last block hash) with a peer, initiating the connection and synchronization process.
 *
 * @param peer_addr The network address of the peer to send the handshake to.
 */
void NodeApp::send_handshake(const std::string& peer_addr, const std::string& target_peer_id) {
    // Read blockchain state thread-safely
    chrono_util::Bytes current_block_hash;
    uint64_t current_height = 0;
    {
        std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
        current_block_hash = last_block_hash_;
        current_height = next_block_height_;
    }

    chrono_p2p::P2PMessage handshake_envelope;
    chrono_p2p::HandshakeMessage* handshake_msg = handshake_envelope.mutable_handshake();

    handshake_msg->set_node_id(status_.node_id);
    handshake_msg->set_protocol_version(1); // Assuming protocol version 1 for now
    size_t colon_pos = status_.p2p_address.find(':');
    if (colon_pos != std::string::npos) {
        handshake_msg->set_port(std::stoi(status_.p2p_address.substr(colon_pos + 1)));
    } else {
        handshake_msg->set_port(cfg_.listen_port); // Fallback
    }
    handshake_msg->set_last_block_hash(current_block_hash.data(), current_block_hash.size());
    handshake_msg->set_current_block_height(current_height > 0 ? current_height - 1 : 0); // current height is next_block_height_ - 1
    handshake_msg->set_genesis_hash(chrono_util::bytes_to_hex(genesis_block_hash_));
    handshake_msg->set_max_total_supply(cfg_.max_total_supply);
    if (external_time_manager_) {
        handshake_msg->set_time_tier(external_time_manager_->get_current_tier());
        handshake_msg->set_time_quality_score(external_time_manager_->get_time_quality_score());
    } else {
        handshake_msg->set_time_tier(5); // Default to lowest tier if manager not ready
        handshake_msg->set_time_quality_score(0.0);
    }

    if (!target_peer_id.empty()) {
        gossip_->send_direct(target_peer_id, handshake_envelope);
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Sent direct handshake to peer: {} ({})", peer_addr, target_peer_id);
    } else {
        gossip_->publish("handshake", handshake_envelope);
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Sent handshake to peer: {}", peer_addr);
    }
}

/**
 * @brief Updates the reputation score of a peer.
 *
 * Positive scores are given for valid interactions, negative for invalid or malicious behavior.
 * This score is used by the `manage_peers` function to maintain a healthy set of network connections.
 *
 * @param peer_id The unique ID of the peer to update.
 * @param score_change The value to add to the peer's score (can be negative).
 * @param increment_invalid_count If true, also increments the peer's invalid message counter.
 */
void NodeApp::update_peer_score(const std::string& peer_id, int score_change, bool increment_invalid_count) {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    auto it = connected_peers_.find(peer_id);
    if (it != connected_peers_.end()) {
        PeerInfo& peer = it->second;
        peer.score += score_change;
        peer.last_seen_time = std::chrono::steady_clock::now(); // Update last seen time

        if (increment_invalid_count) {
            peer.invalid_messages_count++;
            LOG_WARN(chrono_util::LogCategory::P2P, "Peer {}: Invalid message count incremented to {}. Score: {}",
                     peer_id, peer.invalid_messages_count, peer.score);
        } else {
            LOG_DEBUG(chrono_util::LogCategory::P2P, "Peer {}: Score changed by {}. New score: {}", peer_id, score_change, peer.score);
        }
    } else {
        LOG_WARN(chrono_util::LogCategory::P2P, "Attempted to update score for unknown peer: {}", peer_id);
    }
}


/**
 * @brief Periodically prunes bad peers.
 *
 * This function iterates through the list of connected peers and disconnects any that
 * have a score below a certain threshold or have sent too many invalid messages.
 * This helps protect the node from faulty or malicious actors.
 */
void NodeApp::manage_peers() {
    const int LOW_SCORE_THRESHOLD = -10; // Peers below this score might be disconnected
    const int DISCONNECT_INVALID_COUNT_THRESHOLD = 5; // Peers with this many invalid messages might be disconnected

    std::lock_guard<std::mutex> lock(peers_mutex_);
    for (auto it = connected_peers_.begin(); it != connected_peers_.end(); ) {
        PeerInfo& peer = it->second;
        if (peer.score < LOW_SCORE_THRESHOLD || peer.invalid_messages_count >= DISCONNECT_INVALID_COUNT_THRESHOLD) {
            LOG_WARN(chrono_util::LogCategory::P2P, "Disconnecting from peer {} due to low score ({}) or excessive invalid messages ({})",
                     peer.node_id, peer.score, peer.invalid_messages_count);
            // Implement actual disconnection logic
            if (transport_) {

            }
            it = connected_peers_.erase(it);
            status_.connected_peers--;
        } else {
            ++it;
        }
    }
}

/**
 * @brief Restores the node's state from a snapshot.
 *
 * This method loads a previously saved snapshot and reconstructs the ledger state,
 * blockchain height, and last block hash for fast synchronization.
 *
 * @param snapshot_data The snapshot data to restore from.
 */
void NodeApp::start_from_snapshot(const chrono_storage::SnapshotData& snapshot_data) {
    try {
        // Deserialize state from snapshot bytes
        if (!state_.deserialize_from_bytes(snapshot_data.state_bytes)) {
            LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to deserialize state from snapshot at height {}", snapshot_data.height);
            return;
        }

        // Update blockchain state variables
        {
            std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
            last_block_hash_ = snapshot_data.last_block_hash;
            next_block_height_ = snapshot_data.height + 1;  // Next block height is one after snapshot height
        }

        // Persist the metadata to blockchain storage
        Bytes height_bytes;
        uint64_t height_value = snapshot_data.height + 1;
        chrono_util::write_fixed_uint64_le(height_value, height_bytes);
        blockchain_storage_->saveMetadata(NEXT_BLOCK_HEIGHT_KEY, height_bytes);
        blockchain_storage_->saveMetadata(LAST_BLOCK_HASH_KEY, snapshot_data.last_block_hash);

        LOG_INFO(chrono_util::LogCategory::STORAGE, 
                 "Successfully restored state from snapshot at height {}. Next block height: {}", 
                 snapshot_data.height, height_value);

    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Error restoring from snapshot: {}", e.what());
    }
}

/**
 * @brief Manages block synchronization with the network.
 *
 * This method handles the continuous sync process:
 * 1. Checks if we're behind the network (peer heights > local height)
 * 2. Initiates sync with best available peer (highest height, good score)
 * 3. Monitors sync progress and detects timeouts
 * 4. Switches to different peer if current sync stalls
 * 5. Completes sync when caught up with network
 *
 * Called periodically from main event loop to maintain network synchronization.
 */
void NodeApp::manage_sync() {
    uint64_t local_height;
    {
        std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
        local_height = next_block_height_ - 1; // Current blockchain tip height
    }

    // Find highest peer height to determine if we need sync
    uint64_t max_peer_height = 0;
    std::string best_peer_id;
    int best_peer_score = -999;

    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (const auto& [peer_id, peer_info] : connected_peers_) {
            if (peer_info.current_block_height > max_peer_height) {
                max_peer_height = peer_info.current_block_height;
                // Prefer peers with better scores for sync
                if (peer_info.score > best_peer_score) {
                    best_peer_id = peer_id;
                    best_peer_score = peer_info.score;
                }
            }
        }
    }

    // Check if we need to sync (peer chain is longer)
    if (max_peer_height > local_height) {
        if (!is_syncing_) {
            // Start new sync session
            if (!best_peer_id.empty()) {
                start_sync_with_peer(best_peer_id, max_peer_height);
            }
        } else {
            // Check for sync timeout (no progress in SYNC_TIMEOUT_SECONDS)
            auto now = std::chrono::steady_clock::now();
            auto time_since_progress = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_sync_progress_time_).count();

            if (time_since_progress > SYNC_TIMEOUT_SECONDS) {
                LOG_WARN(chrono_util::LogCategory::P2P, 
                         "Sync timeout detected. No progress for {} seconds with peer {}.",
                         time_since_progress, sync_peer_id_);
                
                // Find alternative peer (excluding current sync_peer_id_)
                std::string alternative_peer_id;
                int best_alt_score = -999;
                
                {
                    std::lock_guard<std::mutex> lock(peers_mutex_);
                    for (const auto& [peer_id, peer_info] : connected_peers_) {
                        if (peer_id != sync_peer_id_ && peer_info.current_block_height > local_height) {
                             if (peer_info.score > best_alt_score) {
                                alternative_peer_id = peer_id;
                                best_alt_score = peer_info.score;
                            }
                        }
                    }
                }

                if (!alternative_peer_id.empty()) {
                     LOG_INFO(chrono_util::LogCategory::P2P, "Switching sync to alternative peer {}.", alternative_peer_id);
                     is_syncing_ = false;
                     start_sync_with_peer(alternative_peer_id, max_peer_height);
                } else {
                     LOG_ERROR(chrono_util::LogCategory::P2P, "No alternative peers found for sync. Degrading.");
                     degrade_to_light_sync();
                     is_syncing_ = false; // Stop syncing attempt
                }
            }
        }
    } else {
        // We're caught up with network
        if (is_syncing_) {
            auto sync_duration = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - sync_start_time_).count();
            
            LOG_INFO(chrono_util::LogCategory::P2P, 
                     "Block synchronization complete! Downloaded {} blocks in {} seconds. Current height: {}",
                     sync_downloaded_blocks_, sync_duration, local_height);
            
            // End sync session
            is_syncing_ = false;
            sync_peer_id_.clear();
            sync_downloaded_blocks_ = 0;
            sync_target_height_ = 0;
        }
    }
}

/**
 * @brief Initiates block synchronization with a specific peer.
 *
 * Sets up sync state tracking and sends initial GetBlocks request to peer.
 * Sync will continue via manage_sync() calls until target height is reached.
 *
 * @param peer_id ID of peer to synchronize from.
 * @param target_height Blockchain height to sync to.
 */
void NodeApp::start_sync_with_peer(const std::string& peer_id, uint64_t target_height) {
    uint64_t local_height;
    chrono_util::Bytes current_block_hash;
    
    {
        std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
        local_height = next_block_height_ - 1;
        current_block_hash = last_block_hash_;
    }

    // Initialize sync state
    is_syncing_ = true;
    sync_peer_id_ = peer_id;
    sync_target_height_ = target_height;
    sync_downloaded_blocks_ = 0;
    sync_start_time_ = std::chrono::steady_clock::now();
    last_sync_progress_time_ = sync_start_time_;

    LOG_INFO(chrono_util::LogCategory::P2P, 
             "Starting block synchronization with peer {}. Local height: {}, Target height: {}, Blocks behind: {}",
             peer_id, local_height, target_height, (target_height - local_height));

    // Request first batch of blocks
    chrono_p2p::P2PMessage get_blocks_envelope;
    auto* get_blocks_msg = get_blocks_envelope.mutable_get_blocks();
    get_blocks_msg->set_from_block_hash(current_block_hash.data(), current_block_hash.size());
    get_blocks_msg->set_limit(SYNC_BATCH_SIZE);

    gossip_->publish("get_blocks", get_blocks_envelope);
    
    LOG_INFO(chrono_util::LogCategory::P2P, 
             "Requested {} blocks from peer {} starting from {}",
             SYNC_BATCH_SIZE, peer_id, chrono_util::bytes_to_hex(current_block_hash));
}

/**
 * @brief Detects blockchain forks and initiates resolution.
 *
 * Forks occur when two nodes have different blocks at the same height.
 * This can happen due to:
 * - Network partitions
 * - Simultaneous block proposals
 * - Byzantine behavior
 *
 * Fork resolution strategy:
 * 1. Detect: Compare block hashes at same height with peer
 * 2. Validate: Request peer's chain segment for validation
 * 3. Resolve: Choose longest valid chain (or highest cumulative difficulty)
 * 4. Reorganize: If peer chain is better, revert local blocks and apply peer blocks
 *
 * @param peer_block_hash Hash of block at peer's blockchain.
 * @param block_height Height of the potentially forked block.
 * @param peer_id ID of peer reporting different block.
 * @return True if fork detected and resolution initiated.
 */
bool NodeApp::detect_and_resolve_fork(const chrono_util::Bytes& peer_block_hash, 
                                       uint64_t block_height, 
                                       const std::string& peer_id) {
    // Get local block at same height
    uint64_t local_height;
    chrono_util::Bytes local_block_hash;
    
    {
        std::lock_guard<std::mutex> lock(blockchain_state_mutex_);
        local_height = next_block_height_ - 1;
        
        // Only check for fork if heights match
        if (block_height != local_height) {
            return false; // Not a fork, just different heights
        }
        
        local_block_hash = last_block_hash_;
    }

    // Compare hashes at same height
    if (local_block_hash == peer_block_hash) {
        return false; // No fork, blocks match
    }

    // FORK DETECTED
    LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
             "FORK DETECTED at height {}! Local hash: {}, Peer {} hash: {}",
             block_height,
             chrono_util::bytes_to_hex(local_block_hash),
             peer_id,
             chrono_util::bytes_to_hex(peer_block_hash));

    // In a BFT system, finalized blocks are immutable.
    // If we have finalized a block at this height, we reject the peer's conflicting block.
    // We request the conflicting block for analysis/debugging purposes.
    request_block_from_network(peer_block_hash);

    update_peer_score(peer_id, -2, false); // Small penalty for fork (might be legitimate)
    
    return true; // Fork detected
}

/**
 * @brief Broadcasts a request for a specific block from the network.
 * @param block_hash The hash of the block to request.
 */
void NodeApp::request_block_from_network(const chrono_util::Bytes& block_hash) {
    P2PMessage get_blocks_envelope;
    auto* get_blocks_msg = get_blocks_envelope.mutable_get_blocks();
    get_blocks_msg->set_from_block_hash(block_hash.data(), block_hash.size());
    get_blocks_msg->set_limit(1); // Request just this block
    gossip_->publish("get_blocks", get_blocks_envelope);
    LOG_INFO(chrono_util::LogCategory::P2P, "Requested block {} from network.", bytes_to_hex(block_hash));
}

/**
 * @brief Computes message hash for BFT message signature verification.
 *
 * Creates a deterministic hash of all message fields except the signature.
 * The message format is: height || round || block_hash || validator_id
 * Using Blake3 for deterministic, collision-resistant hashing.
 *
 * @param height Block height in consensus message.
 * @param round Consensus round number.
 * @param block_hash Block hash being voted on (32 bytes).
 * @param validator_id ID of validator sending message.
 * @param time_tier Time Tier of the validator (1-5).
 * @return Blake3 hash of the message content (32 bytes).
 */
chrono_util::Bytes NodeApp::compute_bft_message_hash(uint64_t height, uint32_t round, 
                                                      const chrono_util::Bytes& block_hash, 
                                                      const std::string& validator_id,
                                                      uint32_t time_tier) const {
    // Serialize message components: height (8) || round (4) || block_hash || validator_id_len (4) || validator_id || time_tier (4)
    std::vector<uint8_t> message;
    
    // Height (uint64, little-endian)
    uint8_t height_bytes[8];
    std::memcpy(height_bytes, &height, 8);
    message.insert(message.end(), height_bytes, height_bytes + 8);
    
    // Round (uint32, little-endian)
    uint8_t round_bytes[4];
    std::memcpy(round_bytes, &round, 4);
    message.insert(message.end(), round_bytes, round_bytes + 4);
    
    // Block hash (32 bytes)
    message.insert(message.end(), block_hash.begin(), block_hash.end());
    
    // Validator ID length (uint32, little-endian)
    uint32_t validator_id_len = validator_id.size();
    uint8_t len_bytes[4];
    std::memcpy(len_bytes, &validator_id_len, 4);
    message.insert(message.end(), len_bytes, len_bytes + 4);
    
    // Validator ID string
    message.insert(message.end(), validator_id.begin(), validator_id.end());

    // Time Tier (uint32, little-endian)
    uint8_t tier_bytes[4];
    std::memcpy(tier_bytes, &time_tier, 4);
    message.insert(message.end(), tier_bytes, tier_bytes + 4);
    
    // Hash the entire message with Blake3
    Bytes message_bytes(message.begin(), message.end());
    return chrono_crypto::blake3(message_bytes);
}

/**
 * @brief Verifies signature on a BFT message.
 *
 * Looks up the validator's public key from the configuration, computes the message hash,
 * and verifies the signature matches. Logs detailed information on verification failure.
 *
 * @param validator_id ID of validator that sent the message.
 * @param message_hash Hash of message content to verify.
 * @param signature The signature bytes to verify.
 * @return True if signature is valid and validator is recognized, false otherwise.
 */
bool NodeApp::verify_bft_message_signature(const std::string& validator_id, 
                                            const chrono_util::Bytes& message_hash, 
                                            const chrono_util::Bytes& signature) const {
    // Look up validator's public key
    Bytes validator_public_key = get_validator_public_key(validator_id);
    if (validator_public_key.empty()) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                 "Signature verification failed: validator {} not in validator set", 
                 validator_id);
        return false;
    }
    
    // Verify signature using signer
    if (!signer_->verify(validator_public_key, message_hash, signature)) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                 "Signature verification failed for validator {}. Message hash: {}, Signature size: {}",
                 validator_id, bytes_to_hex(message_hash), signature.size());
        return false;
    }
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
             "Signature verified successfully for validator {}",
             validator_id);
    return true;
}

/**
 * @brief Looks up public key for a validator from configuration.
 *
 * Searches the configured validators list for a matching address/ID and returns
 * the corresponding public key. The validators list comes from the config file.
 *
 * IMPORTANT: In current implementation, validators are stored as Base58Check-encoded
 * addresses. For full signature verification, this should be extended to store
 * full public keys or maintain a mapping of address → public_key.
 *
 * @param validator_id Address/ID of validator.
 * @return Public key bytes if validator found, empty bytes otherwise.
 */
chrono_util::Bytes NodeApp::get_validator_public_key(const std::string& validator_id) const {
    // 1. Try to find in NodeRegistry (on-chain source of truth)
    auto node_opt = state_.get_node_registry().get_node(validator_id);
    if (node_opt && !node_opt->public_key.empty()) {
        try {
            return chrono_util::hex_to_bytes(node_opt->public_key);
        } catch (const std::exception& e) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                     "Failed to decode public key for validator {}: {}", validator_id, e.what());
        }
    }

    // 2. Fallback: Config validators are hex-encoded public keys.
    // If the validator_id matches a key in the config, decode it directly.
    for (const auto& validator_pubkey_hex : cfg_.validators) {
        if (validator_pubkey_hex == validator_id) {
            // The config entry IS the hex public key — decode and return it.
            try {
                return chrono_util::hex_to_bytes(validator_pubkey_hex);
            } catch (const std::exception& e) {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                         "Failed to decode config public key for validator {}: {}", validator_id, e.what());
            }
            return Bytes();
        }
    }
    
    return Bytes(); // Not found
}

/**
 * @brief Validates a HandshakeMessage for required fields and value ranges.
 *
 * Checks that all required fields are present and within reasonable ranges:
 * - node_id must be non-empty and within length limits
 * - port must be valid (1-65535)
 * - current_block_height is allowed to be 0 (genesis)
 * - last_block_hash should be EXPECTED_HASH_SIZE if height > 0
 *
 * @param msg The HandshakeMessage to validate.
 * @param sender_id The ID of the sender for logging purposes.
 * @return True if valid, false if validation failed.
 */
bool NodeApp::validate_handshake_message(const chrono_p2p::HandshakeMessage& msg, const std::string& sender_id) const {
    // Check node_id
    if (msg.node_id().empty()) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Handshake from {} has empty node_id", sender_id);
        return false;
    }
    
    if (msg.node_id().length() < MIN_NODE_ID_LENGTH || msg.node_id().length() > MAX_NODE_ID_LENGTH) {
        LOG_WARN(chrono_util::LogCategory::P2P, 
                 "Handshake from {} has invalid node_id length: {} (expected {}-{})",
                 sender_id, msg.node_id().length(), MIN_NODE_ID_LENGTH, MAX_NODE_ID_LENGTH);
        return false;
    }
    
    // Check port
    if (msg.port() == 0 || msg.port() > MAX_PORT_NUMBER) {
        LOG_WARN(chrono_util::LogCategory::P2P, 
                 "Handshake from {} has invalid port: {} (expected 1-{})",
                 sender_id, msg.port(), MAX_PORT_NUMBER);
        return false;
    }
    
    // Check last_block_hash size if height > 0
    if (msg.current_block_height() > 0 && msg.last_block_hash().size() != EXPECTED_HASH_SIZE) {
        LOG_WARN(chrono_util::LogCategory::P2P,
                 "Handshake from {} has invalid last_block_hash size: {} (expected {})",
                 sender_id, msg.last_block_hash().size(), EXPECTED_HASH_SIZE);
        return false;
    }
    
    // Check max_total_supply
    if (msg.max_total_supply() != cfg_.max_total_supply) {
        LOG_WARN(chrono_util::LogCategory::P2P, 
                  "Peer {} has incompatible max_total_supply: {} vs our {}",
                  sender_id, msg.max_total_supply(), cfg_.max_total_supply);
        return false;
    }

    // Check genesis_hash
    if (!msg.genesis_hash().empty() && msg.genesis_hash() != chrono_util::bytes_to_hex(genesis_block_hash_)) {
        LOG_WARN(chrono_util::LogCategory::P2P,
                  "Peer {} has different genesis hash: {} vs our {}",
                  sender_id, msg.genesis_hash(), chrono_util::bytes_to_hex(genesis_block_hash_));
        return false;
    }
    
    return true;
}

/**
 * @brief Validates a BFT message (Prevote/Precommit/NewRound) for required fields and ranges.
 *
 * Checks that:
 * - height is > 0 (no BFT messages for genesis)
 * - round is >= 0 (already enforced by uint32_t type)
 * - block_hash is exactly EXPECTED_HASH_SIZE bytes
 * - validator_id is non-empty and within length limits
 *
 * @param height Block height from message.
 * @param round Consensus round from message.
 * @param block_hash Block hash from message.
 * @param validator_id Validator ID from message.
 * @param message_type Type of BFT message (for logging).
 * @param sender_id The ID of the sender for logging purposes.
 * @return True if valid, false if validation failed.
 */
bool NodeApp::validate_bft_message(uint64_t height, uint32_t round, const chrono_util::Bytes& block_hash,
                                   const std::string& validator_id, const std::string& message_type,
                                   const std::string& sender_id) const {
    // Check height (must be > 0, no BFT for genesis)
    if (height == 0) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                 "{} from {} has invalid height: 0 (BFT messages require height > 0)",
                 message_type, sender_id);
        return false;
    }
    
    // Check block hash size
    if (block_hash.size() != EXPECTED_HASH_SIZE) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                 "{} from {} has invalid block_hash size: {} (expected {})",
                 message_type, sender_id, block_hash.size(), EXPECTED_HASH_SIZE);
        return false;
    }
    
    // Check validator_id
    if (validator_id.empty()) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                 "{} from {} has empty validator_id",
                 message_type, sender_id);
        return false;
    }
    
    if (validator_id.length() < MIN_NODE_ID_LENGTH || validator_id.length() > MAX_NODE_ID_LENGTH) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                 "{} from {} has invalid validator_id length: {} (expected {}-{})",
                 message_type, sender_id, validator_id.length(), MIN_NODE_ID_LENGTH, MAX_NODE_ID_LENGTH);
        return false;
    }
    
    return true;
}

/**
 * @brief Validates GetBlocks request message.
 *
 * Checks that:
 * - from_block_hash is exactly EXPECTED_HASH_SIZE bytes
 * - limit is > 0 and <= MAX_GET_BLOCKS_LIMIT (prevent DoS via large requests)
 *
 * @param msg The GetBlocksMessage to validate.
 * @param sender_id The ID of the sender for logging purposes.
 * @return True if valid, false if validation failed.
 */
bool NodeApp::validate_get_blocks_message(const chrono_p2p::GetBlocksMessage& msg, const std::string& sender_id) const {
    // Check from_block_hash size
    if (msg.from_block_hash().size() != EXPECTED_HASH_SIZE) {
        LOG_WARN(chrono_util::LogCategory::P2P,
                 "GetBlocks from {} has invalid from_block_hash size: {} (expected {})",
                 sender_id, msg.from_block_hash().size(), EXPECTED_HASH_SIZE);
        return false;
    }
    
    // Check limit (prevent DoS)
    if (msg.limit() == 0) {
        LOG_WARN(chrono_util::LogCategory::P2P,
                 "GetBlocks from {} has invalid limit: 0 (must be > 0)",
                 sender_id);
        return false;
    }
    
    if (msg.limit() > MAX_GET_BLOCKS_LIMIT) {
        LOG_WARN(chrono_util::LogCategory::P2P,
                 "GetBlocks from {} has excessive limit: {} (max: {})",
                 sender_id, msg.limit(), MAX_GET_BLOCKS_LIMIT);
        return false;
    }
    
    return true;
}

void NodeApp::set_time_tier_for_testing(uint32_t tier) {
    if (external_time_manager_) {
        external_time_manager_->set_tier_for_testing(tier);
    }
}

uint64_t NodeApp::calculate_block_reward(uint64_t height) const {
    if (!cfg_.minting_enabled || cfg_.reward_halving_interval == 0) {
        return 0;
    }
    
    uint64_t halvings = height / cfg_.reward_halving_interval;
    uint64_t reward = cfg_.initial_block_reward_nanos;
    
    // Apply halving (divide by 2^halvings)
    for (uint64_t i = 0; i < halvings && reward > 1; ++i) {
        reward /= 2;
    }
    return reward;
}

/**
 * @brief Switches the node to degraded light sync mode.
 */
void NodeApp::degrade_to_light_sync() {
    if (!is_degraded_mode_) {
        is_degraded_mode_ = true;
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Node entering degraded mode: light sync only due to lack of viable full sync peers.");
        // In a real implementation, we might switch storage backend or validation logic here.
        // For now, this flag can be used to skip full block download/verification.
    }
}

void NodeApp::broadcast_message(const std::string& topic, const chrono_p2p::P2PMessage& msg) {
    // Serialize to check size
    std::string serialized = msg.SerializeAsString();
    
    if (serialized.size() <= chrono_p2p::FragmentationManager::MAX_FRAGMENT_SIZE) {
        gossip_->publish(topic, msg);
        return;
    }

    // Fragment
    // Use hash of content as ID
    std::string msg_id = chrono_util::bytes_to_hex(chrono_crypto::blake3(chrono_util::Bytes(serialized.begin(), serialized.end())));
    
    // Determine original type and inner serialized data
    uint32_t original_type = 0;
    std::string inner_serialized;

    if (msg.has_block()) {
        original_type = 3;
        inner_serialized = msg.block().SerializeAsString();
    } else if (msg.has_transaction()) {
        original_type = 5;
        inner_serialized = msg.transaction().SerializeAsString();
    } else if (msg.has_snapshot_chunk()) {
        original_type = 15;
        inner_serialized = msg.snapshot_chunk().SerializeAsString();
    }
    
    if (original_type == 0) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Cannot fragment message type {}. Sending as is.", msg.payload_case());
        gossip_->publish(topic, msg);
        return;
    }
    
    auto fragments = fragmentation_manager_.fragment_message(msg_id, inner_serialized, original_type);
    
    LOG_INFO(chrono_util::LogCategory::P2P, "Fragmenting message {} (type {}) into {} fragments.", msg_id, original_type, fragments.size());

    for (const auto& frag : fragments) {
        chrono_p2p::P2PMessage frag_msg;
        *frag_msg.mutable_fragment() = frag;
        gossip_->publish(topic, frag_msg);
    }
}

} // namespace chrono_node

