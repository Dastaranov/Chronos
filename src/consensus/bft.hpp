//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bft.hpp
 * @brief Defines the BftGadget class, a core component for BFT consensus.
 *
 * This file provides the declarations for the BFT (Byzantine Fault Tolerant) consensus
 * engine, named BftGadget. It includes the definition of the consensus state machine (`BftState`),
 * the structure for consensus decisions (`BftDecision`), and the main `BftGadget` class
 * which orchestrates the consensus process. The gadget is responsible for handling proposals,
 * processing votes (prevotes and precommits), and ultimately deciding on block finality.
 */

#pragma once

#include "ledger/block.hpp"
#include "bft_messages.pb.h"             // Include our BFT message definitions (Protobuf generated)
#include "util/bytes.hpp"             // For BLAKE3 hashing
#include "crypto/signer.hpp"             // For ISigner interface
#include <optional>
#include <map> // For storing votes
#include <set> // For storing unique validator IDs

namespace chrono_consensus {

/**
 * @enum BftState
 * @brief Represents the different states of the BFT consensus state machine.
 *
 * The BFT gadget transitions through these states as it works to finalize a block.
 * Each state corresponds to a specific phase in the consensus protocol.
 */
/**
 * @enum BftState
 * @brief Represents the current state in the BFT consensus state machine.
 * 
 * STATE TRANSITION DIAGRAM:
 * ═════════════════════════════════════════════════════════════════════════════
 * 
 *     INITIAL (height = 0, round = 0)
 *         │
 *         │ [handle_new_round received or new height started]
 *         ▼
 *     NEW_ROUND (waiting for proposal)
 *         │
 *         │ [Leader proposal received, valid block]
 *         ▼
 *     PROPOSE (not actively used, transitions immediately)
 *         │
 *         │ [Node broadcasts prevote for proposed block]
 *         ▼
 *     PREVOTE (collecting prevotes from validators)
 *         │
 *         ├─► [2/3+ prevotes for same block_hash]
 *         │   → Lock block (locked_block_ = proposal, locked_round_ = current_round)
 *         │   → Broadcast precommit
 *         │   → Transition to PRECOMMIT state
 *         │
 *         └─► [Timeout or no quorum] → Advance to next round → NEW_ROUND
 *             (TODO: Timeout handling not yet implemented)
 *         
 *     PRECOMMIT (collecting precommits from validators)
 *         │
 *         ├─► [2/3+ precommits for same block_hash]
 *         │   → Set finalized_block_hash_
 *         │   → Transition to COMMIT state
 *         │
 *         └─► [Timeout or no quorum] → Advance to next round → NEW_ROUND
 *             (TODO: Timeout handling not yet implemented)
 *         
 *     COMMIT (quorum reached, ready for ledger)
 *         │
 *         │ [NodeApp calls check_precommit_quorum() → true]
 *         │ [NodeApp persists block to storage]
 *         ▼
 *     FINALIZED (block committed to ledger)
 *         │
 *         │ [Advance to next height]
 *         ▼
 *     NEW_ROUND (height++, round = 0)
 * 
 * ═════════════════════════════════════════════════════════════════════════════
 * 
 * PROOF-OF-LOCK (POL) MECHANISM:
 * - When a block is locked (locked_block_, locked_round_), node will only prevote
 *   for that block in subsequent rounds UNLESS a NewRound message carries a valid
 *   POL from a higher round, which unlocks the previous block.
 * - This ensures safety: once 2/3+ validators prevote a block, they remain committed
 *   to it unless superseded by evidence of a higher-round decision.
 * 
 * MESSAGE-DRIVEN ARCHITECTURE:
 * - State transitions are triggered by message handlers:
 *   • handle_new_round() → NEW_ROUND state
 *   • handle_prevote()   → PREVOTE state (on quorum)
 *   • handle_precommit() → COMMIT state (on quorum)
 * - No central "tick" or step() function; consensus advances through messages only.
 */
enum class BftState {
  INITIAL,      ///< Initial state before consensus begins for a new height.
  NEW_ROUND,    ///< New round initiated, waiting for leader's proposal.
  PROPOSE,      ///< Leader has proposed a block (not actively used as distinct state).
  PREVOTE,      ///< Node has broadcast prevote, collecting prevotes from validators.
  PRECOMMIT,    ///< Prevote quorum reached, node has broadcast precommit, collecting precommits.
  COMMIT,       ///< Precommit quorum reached, block ready to be committed to ledger.
  FINALIZED     ///< Block successfully committed to ledger, ready for next height.
};

/**
 * @class BftGadget
 * @brief Implements the core logic for the Byzantine Fault Tolerant (BFT) consensus mechanism.
 *
 * This class orchestrates the BFT consensus process. It is stateful and manages rounds,
 * proposals, and votes. It handles incoming BFT messages, determines quorums,
 * selects leaders, and ultimately drives the decision to finalize blocks. It is designed
 * to be a modular component ("gadget") that can be integrated into the main node application.
 */
class BftGadget {
public:
  /**
   * @brief Constructs a BftGadget.
   *
   * Initializes the BFT consensus engine with a set of validators and the ID of this node.
   *
   * @param validators A constant reference to a set of unique validator identifiers.
   * @param my_validator_id The identifier for this node.
   * @param quorum_threshold The proportion of votes required for a quorum (e.g., 0.67 for 2/3). Must be between 0.5 and 1.0.
   * @param round_timeout_ms Timeout duration in milliseconds for a consensus round (default 5000ms).
   */
  BftGadget(const std::set<std::string>& validators, const std::string& my_validator_id, 
            double quorum_threshold = 0.67, int round_timeout_ms = 5000);

  /**
   * @brief Handles an incoming Prevote message from the network.
   *
   * Validates the prevote and, if it's for the current height/round, stores it.
   * If a quorum of prevotes is achieved, this node may proceed to the precommit stage.
   *
   * @param prevote The received Prevote message.
   * @return An optional Precommit message to be broadcast if a prevote quorum is reached.
   */
  std::optional<chronos::bft::Precommit> handle_prevote(const chronos::bft::Prevote& prevote);

  /**
   * @brief Handles an incoming Precommit message from the network.
   *
   * Validates the precommit and, if it's for the current height/round, stores it.
   * If a quorum of precommits is achieved, the block is considered finalized.
   *
   * @param precommit The received Precommit message.
   * @return An optional finalized Block if a precommit quorum is reached.
   */
  std::optional<chrono_ledger::Block> handle_precommit(const chronos::bft::Precommit& precommit);

  /**
   * @brief Handles an incoming NewRound message, signaling a new consensus round.
   *
   * This updates the gadget's state to the new round, clearing previous votes.
   *
   * @param new_round The received NewRound message.
   * @return An optional Prevote message if the new round contains a valid proposal to vote on.
   */
  std::optional<chronos::bft::Prevote> handle_new_round(const chronos::bft::NewRound& new_round);

  /**
   * @brief Deterministically selects a leader for a given height and round.
   *
   * The leader is chosen from the validator set using a deterministic algorithm based on
   * consensus time, height, and round number. This ensures all nodes select the same leader.
   *
   * @param consensus_time A value derived from Proof-of-Time, used as part of the seed.
   * @param height The current block height.
   * @param round The current consensus round.
   * @return The string identifier of the selected leader.
   */
  std::string get_leader_for_round(uint64_t consensus_time, uint64_t height, uint32_t round) const;

  /**
   * @brief Sets the block being proposed for the current round.
   *
   * This is called by the node application when it acts as the leader and creates a new block proposal.
   *
   * @param proposed_block The block to be proposed.
   */
  void set_proposed_block(const chrono_ledger::Block& proposed_block);

  /**
   * @brief Clears the internally stored proposed block.
   *
   * This is typically done at the beginning of a new round or when a proposal is invalidated.
   */
  void clear_proposed_block();

  /**
   * @brief Checks if a block has been proposed for a given height and round.
   *
   * @param height The block height to check.
   * @param round The consensus round to check.
   * @return `true` if a proposal exists for the specified height and round, `false` otherwise.
   */
  bool has_proposed_for_round(uint64_t height, uint32_t round) const;

  /**
   * @brief Checks if a precommit quorum has been reached for a specific height and round.
   *
   * This method examines all received precommits for the current height and round
   * and determines if a quorum has been achieved for any block. A block with a precommit
   * quorum is ready to be finalized.
   *
   * @param height The block height to check (should match current height).
   * @param round The consensus round to check (should match current round).
   * @return `true` if a precommit quorum exists for this height/round, `false` otherwise.
   */
  bool check_precommit_quorum(uint64_t height, uint32_t round) const;

  /**
   * @brief Retrieves the block hash that has achieved a precommit quorum for finalization.
   *
   * If a precommit quorum exists for the current height and round, this method returns
   * the hash of the block that should be finalized. This hash can be used to look up
   * the actual block for finalization.
   *
   * @return An optional containing the block hash if a precommit quorum exists, `std::nullopt` otherwise.
   */
  std::optional<chrono_util::Bytes> get_finalized_block_hash() const;

  /**
   * @brief Gets the current block height of the consensus process.
   * @return The current block height.
   */
  uint64_t get_current_height() const { return current_height_; }
  
  /**
   * @brief Gets the current round number for the current height.
   * @return The current consensus round.
   */
  uint32_t get_current_round() const { return current_round_; }

  /**
   * @brief Gets the currently locked block, if any.
   *
   * A block becomes locked when it receives a prevote quorum. The locked block
   * ensures consensus safety by preventing validators from voting for conflicting blocks.
   *
   * @return An optional containing the locked block if one exists, nullopt otherwise.
   */
  std::optional<chrono_ledger::Block> get_locked_block() const { return locked_block_; }

  /**
   * @brief Gets the round number when the current block was locked.
   * @return The locked round number (0 if no block is locked).
   */
  uint32_t get_locked_round() const { return locked_round_; }

  /**
   * @brief Sets the signer used for creating and verifying BFT message signatures.
   *
   * The BftGadget needs a signer to create signed Prevote and Precommit messages.
   * This should be called after constructing the BftGadget and before starting consensus.
   *
   * @param signer A pointer to an ISigner implementation. The BftGadget does not take ownership.
   */
  void set_signer(chrono_crypto::ISigner* signer) { signer_ = signer; }

  /**
   * @brief Checks if the current round has timed out.
   *
   * A round times out if it has been active for longer than the configured timeout
   * threshold without reaching consensus. This prevents the consensus from getting stuck
   * in a single round indefinitely.
   *
   * @return true if the current round has exceeded the timeout threshold, false otherwise.
   */
  bool is_round_timed_out() const;

  /**
   * @brief Advances to the next consensus round due to timeout or lack of progress.
   *
   * This method increments the round number, clears accumulated votes, and resets
   * the round start timestamp. It should be called when a round timeout is detected
   * or when the consensus needs to move forward without finalizing a block.
   *
   * TIMEOUT RECOVERY MECHANISM:
   * - Clears received prevotes and precommits
   * - Increments round counter
   * - Resets round start time
   * - Maintains locked block (POL safety)
   * - Transitions to NEW_ROUND state
   *
   * @return A NewRound message to be broadcast to all validators, signaling the round change.
   */
  chronos::bft::NewRound advance_to_next_round();

  /**
   * @brief Advances to the next block height after successful block finalization.
   *
   * This method increments the block height, resets the round counter to 0,
   * clears accumulated votes, and resets consensus timing. It should be called
   * after a block has been finalized and added to the ledger.
   *
   * STATE RESET:
   * - Increments height counter
   * - Resets round to 0
   * - Clears all prevotes and precommits
   * - Clears proposed block
   * - Resets round timeout timer
   * - Maintains locked block (if any) for POL mechanism
   * - Transitions to INITIAL state for new height
   *
   * @param new_height The new block height to transition to.
   */
  void advance_to_next_height(uint64_t new_height);

  /**
   * @brief Creates a signed Prevote message for a specific block hash.
   *
   * This method constructs a Prevote protobuf message and signs it using the configured signer.
   * The prevote indicates the node's support for a specific block in the current round.
   *
   * @param block_hash The hash of the block to prevote for.
   * @return A signed Prevote message, or nullopt if no signer is configured.
   */
  std::optional<chronos::bft::Prevote> create_prevote(const chrono_util::Bytes& block_hash);

  /**
   * @brief Creates a signed Precommit message for a specific block hash.
   *
   * This method constructs a Precommit protobuf message and signs it using the configured signer.
   * The precommit indicates the node's commitment to finalizing a specific block.
   *
   * @param block_hash The hash of the block to precommit for.
   * @return A signed Precommit message, or nullopt if no signer is configured.
   */
  std::optional<chronos::bft::Precommit> create_precommit(const chrono_util::Bytes& block_hash);

private:
  ///< @var The set of all validator IDs participating in consensus.
  std::set<std::string> validators_;
  ///< @var The ID of this node's validator.
  std::string my_validator_id_;
  ///< @var The proportion of votes required for a quorum (e.g., > 2/3).
  double quorum_threshold_;
  
  ///< @var Pointer to the signer for creating signed BFT messages (not owned).
  chrono_crypto::ISigner* signer_{nullptr};
  
  ///< @var The current state of the BFT state machine.
  BftState current_state_;
  ///< @var The block height the BFT gadget is currently trying to finalize.
  uint64_t current_height_;
  ///< @var The consensus round for the current height.
  uint32_t current_round_;
  
  ///< @var The block proposed by the leader in the current round.
  std::optional<chrono_ledger::Block> current_proposal_;
  
  ///< @var Stores Prevote messages received for the current round, mapped by validator ID.
  std::map<std::string, chronos::bft::Prevote> received_prevotes_;
  ///< @var Stores Precommit messages received for the current round, mapped by validator ID.
  std::map<std::string, chronos::bft::Precommit> received_precommits_;
  
  ///< @var The block that has received a precommit quorum and is now "locked".
  std::optional<chrono_ledger::Block> locked_block_;
  ///< @var Tracks the round number when locked_block was locked (for POL tracking).
  uint32_t locked_round_{0};
  ///< @var The most recently finalized block.
  std::optional<chrono_ledger::Block> last_finalized_block_;

  // Timeout tracking
  int round_timeout_ms_{5000};     ///< @var Timeout duration for a round in milliseconds.
  std::chrono::steady_clock::time_point round_start_time_; ///< @var Timestamp when current round started.

  /**
   * @brief Calculates the number of validators required for a quorum.
   * @return The minimum number of votes needed for a quorum.
   */
  size_t get_quorum_size() const;
  
  /**
   * @brief Checks if a given ID belongs to a known validator.
   * @param validator_id The validator ID to check.
   * @return `true` if the ID is in the active validator set, `false` otherwise.
   */
  bool is_validator(const std::string& validator_id) const;

  /**
   * @brief Validates that a block hash is not empty.
   * @param block_hash The block hash string to validate.
   * @return true if block_hash is non-empty, false otherwise.
   */
  bool is_valid_block_hash(const std::string& block_hash) const;

  /**
   * @brief Checks if a duplicate prevote has already been received from this validator.
   * @param validator_id The validator to check.
   * @return true if a prevote from this validator already exists, false otherwise.
   */
  bool has_prevote_from_validator(const std::string& validator_id) const;

  /**
   * @brief Checks if a duplicate precommit has already been received from this validator.
   * @param validator_id The validator to check.
   * @return true if a precommit from this validator already exists, false otherwise.
   */
  bool has_precommit_from_validator(const std::string& validator_id) const;

  /**
   * @brief Checks if a quorum of prevotes has been reached for a specific block hash.
   *
   * This helper method examines all received prevotes and determines if enough votes
   * have been cast for the given block hash to form a quorum.
   *
   * @param block_hash The block hash to check for prevote quorum.
   * @return `true` if a quorum of prevotes for this block hash exists, `false` otherwise.
   */
  bool check_prevote_quorum_for_block_hash(const chrono_util::Bytes& block_hash) const;

  /**
   * @brief Retrieves the block hash that has reached a prevote quorum, if any.
   *
   * This helper method analyzes all received prevotes and returns the hash of the block
   * that has received a quorum of votes. This is useful for determining which block to lock
   * or precommit on.
   *
   * @return An optional containing the block hash if a quorum is found, `std::nullopt` otherwise.
   */
  std::optional<chrono_util::Bytes> get_quorum_block_hash_from_prevotes() const;

  /**
   * @brief Retrieves the block hash that has reached a precommit quorum, if any.
   *
   * This helper method analyzes all received precommits and returns the hash of the block
   * that has received a quorum of precommits. This indicates block finalization readiness.
   *
   * @return An optional containing the block hash if a quorum is found, `std::nullopt` otherwise.
   */
  std::optional<chrono_util::Bytes> get_quorum_block_hash_from_precommits() const;
};


} // namespace chrono_consensus
