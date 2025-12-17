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
#include "node/node_app.hpp"
#include "node/node_status.hpp"
#include "util/log.hpp"
#include "util/bytes.hpp" // For hex_to_bytes
#include "util/codec.hpp" // For canonical serialization helpers
#include "storage/IBlockchainStorage.hpp"
#include "storage/DiskBlockchainStorage.hpp" // For default full node storage
#include "storage/file_kv.hpp" // For State's internal IKv
#include "p2p/socket_transport.hpp"
#include "p2p/gossip.hpp"
#include "p2p_messages.pb.h"
#include "bft_messages.pb.h"
#include "consensus/bft_messages.hpp"
#include "rpc/jsonrpc.hpp"
#include "storage/MemoryBlockchainStorage.hpp"
#include "crypto/signer_dilithium.hpp" // New: Include for Dilithium signer
// #include "crypto/signer_hmac.hpp" // Removed: No longer using HMAC signer
#include "consensus/bft.hpp" // Now needed for make_unique

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
      gossip_(std::make_unique<chrono_p2p::Gossip>(std::move(transport_))), // Initialize gossip protocol
      // Initialize cryptographic signer based on configuration
      signer_(nullptr), // Initialize to nullptr first, then assign
      state_kv_store_(std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/chronos_state.kv")), // Setup key-value store for ledger state
      state_(*state_kv_store_), // Initialize ledger state manager
      pot_(cfg_.outlier_mad_factor, cfg_.min_threshold_ms), // Initialize Proof-of-Time aggregator
      bft_(nullptr), // Initialize to nullptr first, then assign after signer_ is ready
      snapshot_manager_(std::make_unique<chrono_storage::SnapshotManager>( // Initialize snapshot manager
                          std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/snapshots.kv"))),
      rpc_(std::make_unique<JsonRpcServer>(cfg_.rpc_port, state_, *this)), // Initialize JSON-RPC server
      blockchain_storage_(nullptr) // Initialize blockchain storage pointer to nullptr
{
    // Initialize Signer
    if (cfg_.private_key.empty()) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Private key is not configured. A validator node cannot run without a private key.");
        throw std::runtime_error("Private key missing in configuration for validator node.");
    }
    try {
        chrono_util::Bytes private_key_bytes = chrono_util::hex_to_bytes(cfg_.private_key);
        signer_ = std::make_unique<chrono_crypto::SignerDilithium>(private_key_bytes);
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to initialize SignerDilithium from private key: {}", e.what());
        throw std::runtime_error("Failed to load private key.");
    }

    // Initialize BFT gadget after signer_ is ready
    bft_ = std::make_unique<chrono_consensus::BftGadget>(
        std::set<std::string>(cfg_.validators.begin(), cfg_.validators.end()), 
        signer_->get_address(),
        cfg_.bft_quorum,
        cfg_.bft_round_timeout_ms);
    
    // Set the signer for BFT message signing
    bft_->set_signer(signer_.get());


    // Create a lambda function for the TimeMeasurement callback.
    // This callback will be invoked by ExternalTimeSourceManager to pass new time measurements
    // to the PoTAggregator.
    auto measurement_callback = [this](const chrono_consensus::TimeMeasurement& tm) {
        this->pot_.add_timestamp(tm);
    };

    // Initialize the ExternalTimeSourceManager with configured NTP servers and query interval.
    // This manager runs in a background thread to continuously fetch time.
    external_time_manager_ = std::make_unique<chrono_consensus::ExternalTimeSourceManager>(
        cfg_.ntp_servers, // List of NTP servers from configuration
        cfg_.ntp_query_interval_ms, // Query interval from configuration
        measurement_callback // Callback to add measurement to PoTAggregator
    );

    // Initialize blockchain_storage_ based on node type specified in configuration.
    // Full nodes use DiskBlockchainStorage for persistent storage, Light nodes use MemoryBlockchainStorage.
    if (cfg_.node_type == NodeType::FULL) {
        blockchain_storage_ = std::make_unique<chrono_storage::DiskBlockchainStorage>(
            std::make_unique<chrono_storage::FileKv>(cfg_.data_dir + "/chronos_blockchain.kv")
        );
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Node operating as FULL node. Using DiskBlockchainStorage.");
    } else { // NodeType::LIGHT
        blockchain_storage_ = std::make_unique<chrono_storage::MemoryBlockchainStorage>(
            std::make_unique<chrono_storage::MemoryKv>()
        );
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Node operating as LIGHT node. Using MemoryBlockchainStorage.");
    }

    LOG_INFO(chrono_util::LogCategory::GENERAL, "NodeApp initialized with data_dir: {}", cfg_.data_dir);

    // Set up node status identifiers and addresses based on configuration.
    status_.node_id = "ChronosNode-" + std::to_string(cfg_.listen_port);
    status_.rpc_address = "127.0.0.1:" + std::to_string(cfg_.rpc_port);
    status_.p2p_address = "127.0.0.1:" + std::to_string(cfg_.listen_port);

    // Set the message handler for incoming P2P messages using the gossip protocol.
    gossip_->set_message_handler(std::bind(&NodeApp::handle_p2p_message, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
}

/**
 * @brief Destructor for NodeApp.
 * Ensures that unique_ptr members are properly destroyed and background threads are stopped.
 */
NodeApp::~NodeApp() {
    // Ensure all background operations are gracefully shut down.
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
    } else {
        // First run: create and store a genesis block
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "No blockchain state found. Creating genesis block...");
        loaded_last_block_hash = Bytes(32, 0); // Zero hash for previous block of genesis

        // Use configured genesis consensus time (or 0 if not configured)
        uint64_t genesis_consensus_time = cfg_.genesis_consensus_time;
        if (genesis_consensus_time == 0) {
            // Fallback to current PoT consensus time if not configured
            genesis_consensus_time = pot_.get_consensus_time();
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Using current consensus time {} for genesis block", genesis_consensus_time);
        }

        Block genesis_block(loaded_last_block_hash, 0, genesis_consensus_time, {});
        
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
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Genesis block created with hash: {}", bytes_to_hex(genesis_block.get_header_hash()));
    }

    // Initialize BFT state to match the loaded blockchain state.
    uint64_t current_bft_height = next_block_height_;
    uint32_t current_bft_round = 0;

    // Initialize timers for periodic tasks
    last_peer_discovery_time_ = std::chrono::steady_clock::now();
    last_peer_management_time_ = std::chrono::steady_clock::now();
    last_snapshot_discovery_time_ = std::chrono::steady_clock::now();

    // Main event loop
    while (true) {
        // Sleep for a short interval, representing a consensus tick or slot
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg_.bft_round_timeout_ms / 10)); // Shorter tick for responsiveness

        // --- Dynamic Peer Discovery ---
        auto now = std::chrono::steady_clock::now();
        if (now - last_peer_discovery_time_ >= peer_discovery_interval_ms_) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Initiating peer discovery.");
            P2PMessage peer_list_envelope;
            auto* peer_list_msg = peer_list_envelope.mutable_peer_list();
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                for (const auto& pair : connected_peers_) {
                    peer_list_msg->add_peers(pair.second.address);
                }
            }
            gossip_->publish("peer_list", peer_list_envelope);
            last_peer_discovery_time_ = now;
        }

        // --- Peer Management ---
        if (now - last_peer_management_time_ >= peer_management_interval_ms_) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Initiating peer management.");
            manage_peers();
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
        uint64_t current_consensus_time = pot_.get_consensus_time();
        
        current_bft_height = bft_->get_current_height(); // Get actual height from BFT gadget
        current_bft_round = bft_->get_current_round();   // Get actual round from BFT gadget

        std::string current_leader = bft_->get_leader_for_round(current_consensus_time, current_bft_height, current_bft_round);

        // Leader logic
        if (current_leader == signer_->get_address()) {
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

                if (!transactions_for_block.empty()) {
                    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "I am the leader for height {} round {}. Proposing block with {} transactions.",
                             current_bft_height, current_bft_round, transactions_for_block.size());

                    // Create a new block
                    Block new_block(last_block_for_proposal, current_bft_height, current_consensus_time, transactions_for_block);
                    
                    // Create a Protobuf NewRound message
                    chronos::bft::NewRound new_round_msg_proto;
                    new_round_msg_proto.set_height(current_bft_height);
                    new_round_msg_proto.set_round(current_bft_round);
                    new_round_msg_proto.set_proposal_block_hash(new_block.get_header_hash().data(), new_block.get_header_hash().size());
                    new_round_msg_proto.set_validator_id(signer_->get_address());
                    
                    chrono_util::Bytes signed_data = signer_->sign_message(new_block.get_header_hash());
                    new_round_msg_proto.mutable_signature()->set_data(signed_data.data(), signed_data.size());

                    // Process this new round message internally, which will trigger this node to prevote for its own block
                    std::optional<chronos::bft::Prevote> self_prevote = bft_->handle_new_round(new_round_msg_proto);
                    if (self_prevote) {
                        P2PMessage prevote_envelope;
                        auto* prevote_proto = prevote_envelope.mutable_prevote();
                        prevote_proto->set_height(self_prevote->height());
                        prevote_proto->set_round(self_prevote->round());
                        prevote_proto->set_block_hash(self_prevote->block_hash().data(), self_prevote->block_hash().size());
                        prevote_proto->set_validator_id(self_prevote->validator_id());
                        prevote_proto->mutable_signature()->set_data(self_prevote->signature().data().c_str(), self_prevote->signature().data().size());
                        gossip_->publish("prevote", prevote_envelope);
                        LOG_INFO(chrono_util::LogCategory::P2P, "Published self-generated Prevote for height {} round {}", self_prevote->height(), self_prevote->round());
                    }

                    // Publish the block itself for followers to validate
                    P2PMessage block_envelope;
                    block_envelope.mutable_block()->set_block_data(new_block.serialize().data(), new_block.serialize().size());
                    gossip_->publish("blocks", block_envelope);
                    LOG_INFO(chrono_util::LogCategory::P2P, "Published new proposed block {} to network.", bytes_to_hex(new_block.get_header_hash()));

                    // Inform BFT gadget that this node has proposed a block
                    bft_->set_proposed_block(new_block);

                } else {
                    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "I am the leader for height {} round {}, but no transactions with sufficient fees in mempool. Waiting.",
                             current_bft_height, current_bft_round);
                    // TODO: Leaders might propose empty blocks or just wait, depending on protocol rules.
                }
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
            
            // TODO: Broadcast new_round_msg via gossip when P2P integration is complete
        }
    }
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
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Attempting to add block with hash: {}", bytes_to_hex(b.get_header_hash()));

    // PoT Consensus Time Validation
    uint64_t current_local_consensus_time = pot_.get_consensus_time();
    const uint64_t CONSENSUS_TIME_TOLERANCE_MS = 10000; 

    if (std::abs(static_cast<int64_t>(b.consensus_time) - static_cast<int64_t>(current_local_consensus_time)) > CONSENSUS_TIME_TOLERANCE_MS) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Block {} has consensus_time {} which is outside acceptable tolerance of local consensus_time {}. Rejecting block.",
                 bytes_to_hex(b.get_header_hash()), b.consensus_time, current_local_consensus_time);
        return false;
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

    for (const auto& tx : b.transactions) {
        if (!state_.apply_transaction(tx)) {
            LOG_WARN(chrono_util::LogCategory::GENERAL, "Failed to apply transaction {} from block {}: Insufficient funds or invalid transaction.", tx.to_string(), bytes_to_hex(b.get_header_hash()));
            // In a real system, this could invalidate the entire block.
        }
    }

    // Persist the block only for full nodes
    if (cfg_.node_type == NodeType::FULL) {
        blockchain_storage_->saveBlock(b);
    }

    // Update and persist blockchain state metadata
    uint64_t new_height = b.height + 1;
    Bytes height_bytes;
    chrono_util::write_fixed_uint64_le(new_height, height_bytes);
    blockchain_storage_->saveMetadata(NEXT_BLOCK_HEIGHT_KEY, height_bytes);
    blockchain_storage_->saveMetadata(LAST_BLOCK_HASH_KEY, b.get_header_hash());

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
    // 1. Basic validity check
    if (!tx.is_valid()) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Rejected transaction (invalid format): {}", tx.to_string());
        return false;
    }

    std::string sender_addr = tx.sender.to_string();
    std::string recipient_addr = tx.recipient.to_string();

    // 2. Recipient address validation
    if (!chrono_address::Address::is_valid(recipient_addr)) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "Rejected transaction (invalid recipient address): {}", tx.to_string());
        return false;
    }

    // 3. Amount validation - zero amount transactions not allowed
    if (tx.amount == 0) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "Rejected transaction (zero amount) from {}: {}", sender_addr, tx.to_string());
        return false;
    }

    // 4. Fee validation - minimum fee requirement
    if (tx.fee < MIN_FEE) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "Rejected transaction (fee too low) from {}: fee={}, minimum={}",
                 sender_addr, tx.fee, MIN_FEE);
        return false;
    }

    // 5. Signature verification
    if (!signer_->verify(tx.sender.get_bytes(), tx.get_hash_for_signing(), tx.signature)) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Rejected transaction (invalid signature) from {}: {}", sender_addr, tx.to_string());
        update_peer_score("local", -5, true);  // Penalize self for bad transaction
        return false;
    }

    // 6. Nonce validation - transaction nonce must match expected next nonce for sender
    uint64_t expected_nonce = state_.get_nonce(sender_addr);
    if (tx.nonce != expected_nonce) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, 
                 "Rejected transaction (nonce mismatch) from {}: expected {}, got {}",
                 sender_addr, expected_nonce, tx.nonce);
        return false;
    }

    // 7. Duplicate detection - check if this exact transaction is already in mempool
    {
        std::lock_guard<std::mutex> lock(mempool_mutex_);
        Bytes tx_hash = tx.get_hash_for_signing();
        for (const auto& existing_tx : mempool_) {
            if (existing_tx.get_hash_for_signing() == tx_hash) {
                LOG_WARN(chrono_util::LogCategory::GENERAL, "Rejected transaction (duplicate): {}", tx.to_string());
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
    LOG_INFO(chrono_util::LogCategory::GENERAL, "Added transaction to mempool: {}", tx.to_string());
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
    gossip_->publish("transactions", tx_envelope);
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
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Selecting transactions for block. Mempool has {} txs.", sortable_txs.size());
    
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

    switch (p2p_msg.payload_case()) {
        case chrono_p2p::P2PMessage::kHandshake: {
            const auto& received_handshake = p2p_msg.handshake();
            
            // VALIDATION 2: Validate handshake message fields
            if (!validate_handshake_message(received_handshake, sender_id)) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Invalid handshake message from {}. Penalizing.", sender_id);
                update_peer_score(sender_id, -5, true);
                break;
            }
            
            LOG_INFO(chrono_util::LogCategory::P2P, "Received Handshake from Node ID: {}, P2P Port: {}, Block Height: {}, Last Block Hash: {}",
                     received_handshake.node_id(), received_handshake.port(),
                     received_handshake.current_block_height(), bytes_to_hex(Bytes(received_handshake.last_block_hash().begin(), received_handshake.last_block_hash().end())));

            PeerInfo peer_info;
            peer_info.node_id = received_handshake.node_id();
            peer_info.address = received_handshake.node_id() + ":" + std::to_string(received_handshake.port());
            peer_info.last_block_hash = Bytes(received_handshake.last_block_hash().begin(), received_handshake.last_block_hash().end());
            peer_info.current_block_height = received_handshake.current_block_height();
            {
                std::lock_guard<std::mutex> lock(peers_mutex_);
                connected_peers_[received_handshake.node_id()] = peer_info;
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
                LOG_WARN(chrono_util::LogCategory::P2P, "Failed to add block {} received from {}. Penalizing sender.", bytes_to_hex(received_block.get_header_hash()), sender_id);
                update_peer_score(sender_id, -5, true);
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
                received_prevote_proto.validator_id()
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
                precommit_proto->mutable_signature()->set_data(precommit_to_send->signature().data().c_str(), precommit_to_send->signature().data().size());
                gossip_->publish("precommit", precommit_envelope);
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
                received_precommit_proto.validator_id()
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
                received_new_round_proto.validator_id()
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
                prevote_proto->mutable_signature()->set_data(prevote_to_send->signature().data().c_str(), prevote_to_send->signature().data().size());
                gossip_->publish("prevote", prevote_envelope);
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "BFT gadget rejected NewRound from {}. Penalizing sender.", sender_id);
                update_peer_score(sender_id, -5, true);
            }
            break;
        }
        case chrono_p2p::P2PMessage::kPeerList: {
            const auto& peer_list_proto = p2p_msg.peer_list();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received PeerListMessage with {} peers.", peer_list_proto.peers_size());
            for (const auto& peer_addr : peer_list_proto.peers()) {
                if (peer_addr == status_.p2p_address || connected_peers_.count(peer_addr)) { 
                    continue;
                }

                size_t colon_pos = peer_addr.find(':');
                if (colon_pos != std::string::npos) {
                    std::string host = peer_addr.substr(0, colon_pos);
                    int port = std::stoi(peer_addr.substr(colon_pos + 1)); // Fixed: was using 'peer' instead of 'peer_addr'
                    if (gossip_->connect_to_peer(host, port)) {
                        LOG_INFO(chrono_util::LogCategory::P2P, "Connected to new peer from PeerList: {}", peer_addr);
                    } else {
                        LOG_WARN(chrono_util::LogCategory::P2P, "Failed to connect to peer from PeerList: {}", peer_addr);
                        update_peer_score(sender_id, -1, true);
                    }
                } else {
                    LOG_WARN(chrono_util::LogCategory::P2P, "Invalid peer address format in PeerList: {}", peer_addr);
                    update_peer_score(sender_id, -1, true);
                }
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
                    P2PMessage get_chunk_envelope;
                    auto* get_chunk_msg = get_chunk_envelope.mutable_get_snapshot_chunk();
                    get_chunk_msg->set_snapshot_height(snapshot_meta.height());
                    get_chunk_msg->set_chunk_index(0); // Request first chunk
                    get_chunk_msg->set_chunk_size(1024 * 1024); // Request 1MB chunks
                    gossip_->publish("get_snapshot_chunk", get_chunk_envelope);
                    // TODO: Manage state for ongoing snapshot download (e.g., store metadata, track chunks)
                }
            }
            break;
        }
        case chrono_p2p::P2PMessage::kGetSnapshotChunk: {
            const auto& get_chunk_req = p2p_msg.get_snapshot_chunk();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received GetSnapshotChunk request from {} for height {} chunk {}.",
                     sender_id, get_chunk_req.snapshot_height(), get_chunk_req.chunk_index());
            
            // For now, send a dummy chunk. In reality, read from snapshot file.
            P2PMessage chunk_envelope;
            auto* chunk_msg = chunk_envelope.mutable_snapshot_chunk();
            chunk_msg->set_snapshot_height(get_chunk_req.snapshot_height());
            chunk_msg->set_chunk_index(get_chunk_req.chunk_index());
            chunk_msg->set_chunk_data("dummy_snapshot_data_chunk", 25); // Placeholder
            chunk_msg->set_is_last_chunk(true); // For now, assume one chunk
            gossip_->publish("snapshot_chunk", chunk_envelope);
            break;
        }
        case chrono_p2p::P2PMessage::kSnapshotChunk: {
            const auto& chunk_msg = p2p_msg.snapshot_chunk();
            LOG_INFO(chrono_util::LogCategory::P2P, "Received SnapshotChunk for height {} chunk {} (last: {}). Size: {}",
                     chunk_msg.snapshot_height(), chunk_msg.chunk_index(), chunk_msg.is_last_chunk(), chunk_msg.chunk_data().size());
            // TODO: Store chunk data and reassemble snapshot.
            if (chunk_msg.is_last_chunk()) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Snapshot reassembly complete for height {}. Ready to restore (TODO).", chunk_msg.snapshot_height());
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
void NodeApp::send_handshake(const std::string& peer_addr) {
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

    gossip_->publish("handshake", handshake_envelope);
    LOG_INFO(chrono_util::LogCategory::GENERAL, "Sent handshake to peer: {}", peer_addr);
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
            start_sync_with_peer(best_peer_id, max_peer_height);
        } else {
            // Check for sync timeout (no progress in SYNC_TIMEOUT_SECONDS)
            auto now = std::chrono::steady_clock::now();
            auto time_since_progress = std::chrono::duration_cast<std::chrono::seconds>(
                now - last_sync_progress_time_).count();

            if (time_since_progress > SYNC_TIMEOUT_SECONDS) {
                LOG_WARN(chrono_util::LogCategory::P2P, 
                         "Sync timeout detected. No progress for {} seconds with peer {}. Switching to peer {}.",
                         time_since_progress, sync_peer_id_, best_peer_id);
                
                // Reset sync and try different peer
                is_syncing_ = false;
                start_sync_with_peer(best_peer_id, max_peer_height);
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

    // TODO: Implement full fork resolution logic
    // For now, we log the fork and penalize the peer slightly (might be legitimate fork)
    // Full implementation should:
    // 1. Request peer's block at this height
    // 2. Validate peer's block (signatures, transactions, etc.)
    // 3. Request previous blocks to find common ancestor
    // 4. Compare chain quality (length, cumulative work, validator signatures)
    // 5. If peer chain is better, reorganize: revert local blocks, apply peer blocks
    // 6. If local chain is better, ignore peer's fork (they need to reorganize)

    update_peer_score(peer_id, -2, false); // Small penalty for fork (might be legitimate)
    
    return true; // Fork detected
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
 * @return Blake3 hash of the message content (32 bytes).
 */
chrono_util::Bytes NodeApp::compute_bft_message_hash(uint64_t height, uint32_t round, 
                                                      const chrono_util::Bytes& block_hash, 
                                                      const std::string& validator_id) const {
    // Serialize message components: height (8) || round (4) || block_hash || validator_id_len (4) || validator_id
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
    // Look up in configured validators list
    // Currently, validators_ in config contains addresses (Base58Check format)
    
    // TODO: This should be extended to map validator address to their public key
    // For now, we validate that the validator_id is in the validator set
    for (const auto& validator_addr : cfg_.validators) {
        if (validator_addr == validator_id) {
            // Validator found in set. In a full implementation, we would:
            // 1. Maintain a mapping of validator address → public key
            // 2. Or derive public key from address (if using address as key)
            // 3. Or retrieve public key from a validator registry contract
            
            // For now, we return a placeholder (should be implemented with actual key storage)
            // This is a known limitation that should be addressed in the validator registry phase
            LOG_DEBUG(chrono_util::LogCategory::CONSENSUS, 
                      "Validator {} found in validator set", validator_id);
            
            // TODO: Implement actual public key lookup
            // Placeholder: return non-empty to indicate validator is recognized
            // Real implementation needs validator_id → public_key mapping
            return Bytes(1, 0); // Non-empty placeholder
        }
    }
    
    // Validator not found in set
    return Bytes(); // Empty bytes
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

} // namespace chrono_node
