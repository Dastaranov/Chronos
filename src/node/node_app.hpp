//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file node_app.hpp
 * @brief Main application class for the Chronos blockchain node.
 *
 * This file declares the `NodeApp` class, which serves as the central orchestrator
 * for all node operations. It integrates various components such as P2P networking,
 * consensus (BFT and PoT), state management, blockchain storage, and the RPC server.
 * It also defines related data structures like `PeerInfo` and constants for storage keys.
 */
#pragma once

#include <memory>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <mutex>
#include "node/config.hpp"
#include "node/node_status.hpp"
#include "ledger/block.hpp"
#include "storage/snapshots.hpp"
#include "util/console_display.hpp"
#include "util/bytes.hpp"
// #include "crypto/signer_hmac.hpp" // Removed: No longer using HMAC signer
#include "p2p/gossip.hpp"
#include "consensus/pot_aggregator.hpp"
// #include "consensus/bft.hpp" // Now using unique_ptr, so forward declaration is enough
#include "consensus/external_time_source_manager.hpp" // NEW: Include ExternalTimeSourceManager header

// Forward declarations to reduce include dependencies and compilation time.
namespace chrono_p2p {
class SocketTransport;
}

namespace chrono_crypto {
class ISigner;
}

namespace chrono_storage {
class IKv;
class IBlockchainStorage;
class SnapshotManager;
}

namespace chrono_ledger {
class State;
class Transaction;
}

namespace chrono_consensus {
class PoTAggregator;
class BftGadget; // Forward declaration
class ExternalTimeSourceManager; // NEW: Forward declaration for ExternalTimeSourceManager
}

namespace chrono_node { class JsonRpcServer; }

namespace chrono_node {

/**
 * @struct PeerInfo
 * @brief Stores information about a connected peer in the network.
 *
 * This structure holds metadata for each peer, including its network address,
 * last known blockchain state, and a reputation score to manage connections.
 */
struct PeerInfo {
    ///< @var Unique identifier for the peer node.
    std::string node_id;
    ///< @var Network address of the peer (e.g., "ip:port").
    std::string address; 
    ///< @var The hash of the last block the peer reported.
    chrono_util::Bytes last_block_hash;
    ///< @var The current block height of the peer.
    uint64_t current_block_height;

    ///< @var A reputation score, adjusted based on peer behavior.
    int score = 0;
    ///< @var Counter for invalid messages received from this peer.
    int invalid_messages_count = 0;
    ///< @var The last time a message was received from this peer.
    std::chrono::steady_clock::time_point last_seen_time = std::chrono::steady_clock::now();
};

/// @brief The key used in the database to store the hash of the last finalized block.
inline const chrono_util::Bytes LAST_BLOCK_HASH_KEY = chrono_util::string_to_bytes("_LAST_BLOCK_HASH_");
/// @brief The key used in the database to store the height of the next block to be created.
inline const chrono_util::Bytes NEXT_BLOCK_HEIGHT_KEY = chrono_util::string_to_bytes("_NEXT_BLOCK_HEIGHT_");
/// @brief Minimum transaction fee (in satoshis or smallest unit). Set to 0 for no minimum, increase for fee-based markets.
inline constexpr uint64_t MIN_FEE = 1;  // Minimum 1 unit fee per transaction

/**
 * @class NodeApp
 * @brief Orchestrates all operations of a Chronos blockchain node.
 *
 * This class ties together all major components of the node:
 * - P2P communication via the `Gossip` and `SocketTransport` modules.
 * - Consensus logic through `BftGadget` and `PoTAggregator`.
 * - State management via the `State` class.
 * - Blockchain and snapshot storage through `IBlockchainStorage` and `SnapshotManager`.
 * - External interaction via the `JsonRpcServer`.
 * It runs the main event loop, processing network messages, driving consensus,
 * and managing the blockchain.
 */
class NodeApp {
public:
  /**
   * @brief Constructs the NodeApp with a given configuration.
   * @param cfg The node's configuration settings.
   */
  explicit NodeApp(const chrono_node::Config& cfg);

  /**
   * @brief Destructor. Required for proper cleanup of unique_ptr to forward-declared types.
   */
  ~NodeApp();

  /**
   * @brief Starts the main execution loop of the node.
   *
   * This function initializes networking, starts the RPC server, and enters the main
   * consensus and peer management loop.
   */
  void run();

  /**
   * @brief Adds a new block to the blockchain.
   *
   * This method validates the block against the current state and consensus rules. If valid,
   * it applies the block's transactions to the state, updates the blockchain, and persists the changes.
   * @param b The `Block` to be added.
   * @return `true` if the block was successfully added, `false` otherwise.
   */
  bool add_block(const chrono_ledger::Block& b);

  /**
   * @brief Adds a new transaction to the mempool and broadcasts it to the network.
   * @param tx The `Transaction` to be added.
   */
  void add_transaction(const chrono_ledger::Transaction& tx);

  /**
   * @brief Adds a transaction to the mempool if it passes basic validation.
   * @param tx The transaction to add.
   * @return `true` if added successfully, `false` otherwise.
   */
  bool add_transaction_to_mempool(const chrono_ledger::Transaction& tx);

  /**
   * @brief Publishes a transaction to the network via the gossip protocol.
   * @param tx The transaction to publish.
   */
  void publish_transaction(const chrono_ledger::Transaction& tx);

  /**
   * @brief Provides access to the blockchain storage interface.
   * @return A pointer to the `IBlockchainStorage` instance.
   */
  chrono_storage::IBlockchainStorage* getBlockchainStorage() const {
      return blockchain_storage_.get();
  }

  /**
   * @brief Provides read-only access to the transaction mempool.
   * 
   * Returns a copy of the mempool to ensure thread-safe access. Holding a reference
   * to the mempool while it may be modified from other threads is unsafe.
   * 
   * @return A copy of the vector of transactions in the mempool.
   */
  std::vector<chrono_ledger::Transaction> get_mempool_const() const {
      std::lock_guard<std::mutex> lock(mempool_mutex_);
      return mempool_;
  }

  /**
   * @brief Selects and prepares transactions for block proposal by the leader.
   *
   * Filters mempool to select valid transactions with sufficient fees, sorted by fee (highest first).
   * Applies transactions sequentially and validates state. Rolls back state on transaction failure.
   * Collects leader rewards from transaction fees.
   *
   * @param mempool The current mempool containing candidate transactions.
   * @return A vector of valid transactions selected for the block, and the collected fees for the leader.
   */
  std::pair<std::vector<chrono_ledger::Transaction>, uint64_t> select_transactions_for_block(
      const std::vector<chrono_ledger::Transaction>& mempool);

  /**
   * @brief Restores the node's state from a given snapshot.
   *
   * This is used to quickly synchronize a node without replaying all blocks from genesis.
   * @param snapshot_data The snapshot data to restore from.
   */
  void start_from_snapshot(const chrono_storage::SnapshotData& snapshot_data);

  // Constants for message validation (public for testing)
  static constexpr size_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024;  ///< 10MB max message size
  static constexpr size_t MAX_BLOCK_SIZE = 5 * 1024 * 1024;     ///< 5MB max block size
  static constexpr size_t MAX_TRANSACTION_SIZE = 100 * 1024;    ///< 100KB max transaction size
  static constexpr size_t EXPECTED_HASH_SIZE = 32;              ///< Expected size of block hashes (Blake3)
  static constexpr uint32_t MAX_GET_BLOCKS_LIMIT = 100;         ///< Max blocks to request at once
  static constexpr size_t MIN_NODE_ID_LENGTH = 10;              ///< Minimum reasonable node ID length
  static constexpr size_t MAX_NODE_ID_LENGTH = 256;             ///< Maximum reasonable node ID length
  static constexpr uint32_t MAX_PORT_NUMBER = 65535;            ///< Maximum valid TCP port number

  /**
   * @brief Validates a HandshakeMessage for required fields and value ranges.
   *
   * @param msg The HandshakeMessage to validate.
   * @param sender_id The ID of the sender for logging purposes.
   * @return True if valid, false if validation failed.
   */
  bool validate_handshake_message(const chrono_p2p::HandshakeMessage& msg, const std::string& sender_id) const;

  /**
   * @brief Validates a BFT message (Prevote/Precommit/NewRound) for required fields and ranges.
   *
   * @param height Block height from message.
   * @param round Consensus round from message.
   * @param block_hash Block hash from message.
   * @param validator_id Validator ID from message.
   * @param message_type Type of BFT message (for logging).
   * @param sender_id The ID of the sender for logging purposes.
   * @return True if valid, false if validation failed.
   */
  bool validate_bft_message(uint64_t height, uint32_t round, const chrono_util::Bytes& block_hash,
                           const std::string& validator_id, const std::string& message_type,
                           const std::string& sender_id) const;

  /**
   * @brief Validates GetBlocks request message.
   *
   * @param msg The GetBlocksMessage to validate.
   * @param sender_id The ID of the sender for logging purposes.
   * @return True if valid, false if validation failed.
   */
  bool validate_get_blocks_message(const chrono_p2p::GetBlocksMessage& msg, const std::string& sender_id) const;

private:
  /**
   * @brief Handles incoming messages from the P2P network.
   *
   * This is the central callback for the gossip protocol, dispatching messages
   * to the appropriate handlers based on their type.
   * @param topic The gossip topic on which the message was received.
   * @param data The raw message data.
   * @param sender_id The ID of the peer that sent the message.
   */
  void handle_p2p_message(const std::string& topic, const chrono_util::Bytes& data, const std::string& sender_id);

  /**
   * @brief Sends a handshake message to a peer.
   *
   * This message shares the node's identity, P2P port, and current blockchain state
   * (height and last block hash) with a peer, initiating the connection and synchronization process.
   *
   * @param peer_addr The network address of the peer to send the handshake to.
   */
  void send_handshake(const std::string& peer_addr);

  /**
   * @brief Broadcasts a request for a specific block from the network.
   * @param block_hash The hash of the block to request.
   */
  void request_block_from_network(const chrono_util::Bytes& block_hash);
  
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
  void update_peer_score(const std::string& peer_id, int score_change, bool increment_invalid_count = false);

  /**
   * @brief Periodically prunes bad peers.
   *
   * This function iterates through the list of connected peers and disconnects any that
   * have a score below a certain threshold or have sent too many invalid messages.
   * This helps protect the node from faulty or malicious actors.
   */
  void manage_peers();

  /**
   * @brief Initiates or continues block synchronization with the network.
   *
   * Selects best peer for sync (highest height, good score), requests next batch of blocks.
   * Handles sync timeout detection and peer switching if current sync stalls.
   */
  void manage_sync();

  /**
   * @brief Starts synchronization with a specific peer.
   *
   * @param peer_id ID of peer to sync from.
   * @param target_height Target blockchain height to sync to.
   */
  void start_sync_with_peer(const std::string& peer_id, uint64_t target_height);

  /**
   * @brief Detects and resolves blockchain forks.
   *
   * Compares received block hash with local chain. If mismatch detected at same height,
   * triggers fork resolution (request peer's chain, validate, potentially reorganize).
   *
   * @param peer_block_hash Hash of block at peer's chain.
   * @param block_height Height of the block.
   * @param peer_id ID of peer reporting this block.
   * @return True if fork detected and resolution initiated.
   */
  bool detect_and_resolve_fork(const chrono_util::Bytes& peer_block_hash, 
                                uint64_t block_height, 
                                const std::string& peer_id);

  /**
   * @brief Computes message hash for BFT message signature verification.
   *
   * Creates deterministic hash of message fields (excluding signature).
   * Used for validating signatures on Prevote, Precommit, and NewRound messages.
   *
   * @param height Block height in consensus message.
   * @param round Consensus round number.
   * @param block_hash Block hash being voted on.
   * @param validator_id ID of validator sending message.
   * @return Blake3 hash of the message content (32 bytes).
   */
  chrono_util::Bytes compute_bft_message_hash(uint64_t height, uint32_t round, 
                                               const chrono_util::Bytes& block_hash, 
                                               const std::string& validator_id) const;

  /**
   * @brief Verifies signature on a BFT message.
   *
   * Validates that the signature was created by the claimed validator using their
   * registered public key. Returns false and logs warning if verification fails.
   *
   * @param validator_id ID of validator that sent the message.
   * @param message_hash Hash of message content to verify.
   * @param signature The signature bytes to verify.
   * @return True if signature is valid, false otherwise.
   */
  bool verify_bft_message_signature(const std::string& validator_id, 
                                     const chrono_util::Bytes& message_hash, 
                                     const chrono_util::Bytes& signature) const;

  /**
   * @brief Looks up public key for a validator from configuration.
   *
   * @param validator_id Address/ID of validator.
   * @return Public key bytes if validator found, empty bytes otherwise.
   */
  chrono_util::Bytes get_validator_public_key(const std::string& validator_id) const;

  // Core configuration and status
  chrono_node::Config cfg_; ///< @var The node's configuration.
  NodeStatus status_;       ///< @var The current status and metrics of the node.

  // P2P networking components
  std::unique_ptr<chrono_p2p::SocketTransport> transport_; ///< @var The underlying network transport layer.
  std::unique_ptr<chrono_p2p::Gossip> gossip_;           ///< @var The gossip protocol manager for P2P communication.

  // Cryptography
  std::unique_ptr<chrono_crypto::ISigner> signer_; ///< @var The cryptographic signer for creating signatures.

  // State and storage
  std::unique_ptr<chrono_storage::IKv> state_kv_store_; ///< @var The key-value store for the state manager.
  chrono_ledger::State state_;                          ///< @var The current state of the ledger (e.g., account balances).
  std::unique_ptr<chrono_storage::IBlockchainStorage> blockchain_storage_; ///< @var The storage interface for the blockchain itself.
  std::unique_ptr<chrono_storage::SnapshotManager> snapshot_manager_;     ///< @var Manages the creation and restoration of state snapshots.

  // Consensus mechanisms
  chrono_consensus::PoTAggregator pot_; ///< @var The Proof-of-Time aggregator.
  std::unique_ptr<chrono_consensus::BftGadget> bft_;       ///< @var The BFT consensus engine. (Now unique_ptr)

  // External Time Source Management
  std::unique_ptr<chrono_consensus::ExternalTimeSourceManager> external_time_manager_; ///< @var Manages external time sources for PoT.

  // RPC Interface
  std::unique_ptr<chrono_node::JsonRpcServer> rpc_; ///< @var The JSON-RPC server for external communication.

  // Mempool
  std::vector<chrono_ledger::Transaction> mempool_; ///< @var A pool of pending transactions.

  // Mutexes for thread-safe access to shared data
  mutable std::mutex mempool_mutex_;         ///< @var Protects access to mempool_.
  mutable std::mutex peers_mutex_;           ///< @var Protects access to connected_peers_.
  mutable std::mutex blockchain_state_mutex_; ///< @var Protects access to last_block_hash_ and next_block_height_.

  // Timing and intervals for periodic tasks
  std::chrono::steady_clock::time_point last_peer_discovery_time_;       ///< @var Timestamp of the last peer discovery broadcast.
  std::chrono::milliseconds peer_discovery_interval_ms_ = std::chrono::seconds(30); ///< @var How often to run peer discovery.
  std::chrono::steady_clock::time_point last_peer_management_time_;        ///< @var Timestamp of the last peer management check.
  std::chrono::milliseconds peer_management_interval_ms_ = std::chrono::seconds(60);   ///< @var How often to run peer management.
  std::chrono::steady_clock::time_point last_snapshot_discovery_time_;     ///< @var Timestamp of the last snapshot discovery query.
  std::chrono::milliseconds snapshot_discovery_interval_ms_ = std::chrono::seconds(120); ///< @var How often to query for available snapshots.


  // Blockchain state trackers
  chrono_util::Bytes last_block_hash_; ///< @var The hash of the most recent block in the chain.
  uint64_t next_block_height_;     ///< @var The height of the next block to be added.
  chrono_util::Bytes genesis_block_hash_; ///< @var The hash of the genesis block.

  /**
   * @brief Calculates the block reward for a given height based on tokenomics.
   * @param height The block height.
   * @return The reward in nanos.
   */
  uint64_t calculate_block_reward(uint64_t height) const;

  // Network synchronization state
  bool is_syncing_ = false;              ///< @var True if node is currently syncing blocks from peers.
  std::string sync_peer_id_;              ///< @var ID of peer we're currently syncing from.
  uint64_t sync_target_height_ = 0;       ///< @var Target height to sync to.
  uint64_t sync_downloaded_blocks_ = 0;   ///< @var Number of blocks downloaded in current sync session.
  std::chrono::steady_clock::time_point sync_start_time_; ///< @var When current sync session started.
  std::chrono::steady_clock::time_point last_sync_progress_time_; ///< @var Last time we received a block during sync.
  static constexpr int SYNC_TIMEOUT_SECONDS = 30; ///< @var Timeout for sync progress (switch peer if no progress).
  static constexpr int SYNC_BATCH_SIZE = 10;      ///< @var Number of blocks to request per batch.

  // Peer management
  std::unordered_map<std::string, PeerInfo> connected_peers_; ///< @var A map of connected peers, keyed by their node ID.

  
};

} // namespace chrono_node