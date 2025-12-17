//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bft.cpp
 * @brief Implements the BftGadget class for handling BFT consensus logic.
 *
 * This file contains the implementation of the BftGadget class, which encapsulates
 * the core logic for a Byzantine Fault Tolerant (BFT) consensus mechanism, similar to
 * Tendermint. It manages the consensus state, handles incoming BFT messages (Prevote,
 * Precommit, NewRound), determines quorum, and selects leaders for rounds.
 *
 * The BftGadget is designed to be a state machine that progresses through different
 * consensus states based on messages received from other validators. It validates
 * messages, checks for quorums, and will eventually be responsible for finalizing blocks.
 */

#include "consensus/bft.hpp"
#include "util/log.hpp"
#include "util/bytes.hpp"
#include "crypto/blake3.hpp" // NEW: For hash_blake3_bytes

namespace chrono_consensus {

/**
 * @brief Constructs a BftGadget instance.
 *
 * Initializes the BFT consensus engine with a set of validators and the ID of the current node.
 * It sets the initial state of the consensus to `INITIAL`, and the starting height and round to 0.
 *
 * @param validators A set of strings, where each string is a unique identifier for a validator.
 * @param my_validator_id The identifier for this node, which should be part of the validator set.
 * @param quorum_threshold The proportion of votes required for a quorum (e.g., 0.67 for 2/3). Must be between 0.5 and 1.0.
 * @param round_timeout_ms Timeout duration in milliseconds for a consensus round.
 * @throws std::invalid_argument if quorum_threshold is not between 0.5 and 1.0.
 */
BftGadget::BftGadget(const std::set<std::string>& validators, const std::string& my_validator_id, 
                     double quorum_threshold, int round_timeout_ms)
    : validators_(validators),
      my_validator_id_(my_validator_id),
      quorum_threshold_(quorum_threshold),
      round_timeout_ms_(round_timeout_ms),
      current_state_(BftState::INITIAL),
      current_height_(0),
      current_round_(0) {
    // Validate quorum threshold
    if (quorum_threshold_ < 0.5 || quorum_threshold_ > 1.0) {
        throw std::invalid_argument("quorum_threshold must be between 0.5 and 1.0, got: " + std::to_string(quorum_threshold_));
    }
    
    // Initialize round start time to now
    round_start_time_ = std::chrono::steady_clock::now();
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
             "BftGadget initialized with {} validators. My ID: {}. Quorum threshold: {:.2f}. Round timeout: {}ms", 
             validators_.size(), my_validator_id_, quorum_threshold_, round_timeout_ms_);
}

/**
 * @brief Calculates the minimum number of votes required to reach a quorum.
 *
 * A quorum in BFT is typically defined as 2/3 of the validators plus one. This method
 * calculates this size based on the total number of validators in the current set.
 * The formula used is floor(2 * N / 3) + 1, where N is the number of validators.
 *
 * @return The size of the quorum as a size_t.
 */
size_t BftGadget::get_quorum_size() const {
    // A quorum is typically 2f + 1, where N = 3f + 1 or N = 3f + 0
    // So for N validators, f = (N-1)/3 (integer division)
    // 2f + 1 = 2 * ((N-1)/3) + 1
    // Or more simply, ceil(2N/3) for safety, or floor(2N/3) + 1
    // Let's use floor(2N/3) + 1 as per common practice (e.g., Tendermint)
    return (validators_.size() * 2) / 3 + 1;
}

/**
 * @brief Checks if a given ID belongs to a known validator.
 *
 * This function verifies if the provided `validator_id` is present in the set of active validators.
 *
 * @param validator_id The identifier of the validator to check.
 * @return true if the ID belongs to a validator, false otherwise.
 */
bool BftGadget::is_validator(const std::string& validator_id) const {
    return validators_.count(validator_id) > 0;
}

/**
 * @brief Handles an incoming Prevote message.
 *
 * This method processes a `Prevote` message from another validator. It performs several checks:
 * - Verifies that the sender is a known validator.
 * - Ensures the prevote is for the current consensus height and round.
 *
 * If the prevote is valid, it's stored. If a quorum of prevotes for the same block is reached,
 * the node's state transitions to `PREVOTE`.
 *
 * @param prevote The Prevote message to handle.
 * @return A `Precommit` message if the node is ready to precommit, otherwise `std::nullopt`.
 *         (Note: Currently returns nullopt as signing logic is a TODO).
 */
std::optional<chronos::bft::Precommit> BftGadget::handle_prevote(const chronos::bft::Prevote& prevote) {
    // 1. Validator check
    if (!is_validator(prevote.validator_id())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received prevote from unknown validator: {}", prevote.validator_id());
        return std::nullopt;
    }
    
    // 2. Height/round check
    if (prevote.height() != current_height_ || prevote.round() != current_round_) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received prevote for incorrect height/round: {}/{} vs current {}/{}",
                 prevote.height(), prevote.round(), current_height_, current_round_);
        return std::nullopt;
    }
    
    // 3. Block hash validation (must not be empty)
    if (!is_valid_block_hash(prevote.block_hash())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received prevote with invalid (empty) block hash from {}", prevote.validator_id());
        return std::nullopt;
    }
    
    // 4. Duplicate detection (ignore if we already have a prevote from this validator)
    if (has_prevote_from_validator(prevote.validator_id())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received duplicate prevote from {}", prevote.validator_id());
        return std::nullopt;
    }

    // Store the valid prevote
    received_prevotes_[prevote.validator_id()] = prevote;
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Received valid prevote from {} for block {}. Total prevotes: {}", 
             prevote.validator_id(), prevote.block_hash().substr(0, 8), received_prevotes_.size());

    // Check for quorum
    if (received_prevotes_.size() >= get_quorum_size()) {
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Quorum of prevotes reached for height {} round {}", current_height_, current_round_);
        
        // STATE TRANSITION: PREVOTE
        // Quorum of prevotes detected. This node transitions to PREVOTE state, indicating
        // that enough validators have voted on a proposal. If a specific block has 2/3+ votes,
        // we lock it and prepare to broadcast a precommit.
        current_state_ = BftState::PREVOTE;

        // Check if there's a specific block that reached quorum
        auto quorum_hash = get_quorum_block_hash_from_prevotes();
        if (quorum_hash.has_value() && current_proposal_.has_value()) {
            // Lock the block if it matches the proposal
            chrono_util::Bytes proposal_hash = current_proposal_->get_header_hash();
            if (quorum_hash.value() == proposal_hash) {
                locked_block_ = current_proposal_;
                locked_round_ = current_round_;
                LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                         "Block locked at height {} round {} with hash {}",
                         current_height_, current_round_, chrono_util::bytes_to_hex(proposal_hash).substr(0, 8));
                
                // Create and return a precommit for the locked block
                return create_precommit(proposal_hash);
            }
        }

        // If no block to lock, just return nullopt
        return std::nullopt;
    }
    return std::nullopt;
}

/**
 * @brief Handles an incoming Precommit message.
 *
 * STATE TRANSITION: This handler transitions to COMMIT state when a precommit quorum
 * (2/3+ validators) is reached for the same block_hash. Once in COMMIT state, the block
 * is ready for finalization by NodeApp.
 *
 * FLOW:
 * 1. Validate sender is a known validator
 * 2. Check height/round match current consensus state
 * 3. Validate block_hash is non-empty
 * 4. Detect duplicates (ignore if already received precommit from this validator)
 * 5. Store precommit and check for quorum (>= 2/3 of validators)
 * 6. If quorum reached:
 *    - Set finalized_block_hash_ to the quorum block_hash
 *    - Transition to COMMIT state
 *    - NodeApp can now call check_precommit_quorum() and finalize the block
 *
 * This method processes a `Precommit` message. It checks if the sender is a known validator
 * and if the message corresponds to the current height and round. Valid precommits are stored.
 * If a quorum of precommits is achieved, the state transitions to `COMMIT`, and the block is
 * considered ready for finalization.
 *
 * @param precommit The Precommit message to handle.
 * @return The finalized `Block` if a quorum is reached, otherwise `std::nullopt`.
 *         (Note: Currently returns nullopt; finalization handled by NodeApp via check_precommit_quorum()).
 */
std::optional<chrono_ledger::Block> BftGadget::handle_precommit(const chronos::bft::Precommit& precommit) {
    // 1. Validator check
    if (!is_validator(precommit.validator_id())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received precommit from unknown validator: {}", precommit.validator_id());
        return std::nullopt;
    }
    
    // 2. Height/round check
    if (precommit.height() != current_height_ || precommit.round() != current_round_) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received precommit for incorrect height/round: {}/{} vs current {}/{}",
                 precommit.height(), precommit.round(), current_height_, current_round_);
        return std::nullopt;
    }
    
    // 3. Block hash validation (must not be empty)
    if (!is_valid_block_hash(precommit.block_hash())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received precommit with invalid (empty) block hash from {}", precommit.validator_id());
        return std::nullopt;
    }
    
    // 4. Duplicate detection (ignore if we already have a precommit from this validator)
    if (has_precommit_from_validator(precommit.validator_id())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received duplicate precommit from {}", precommit.validator_id());
        return std::nullopt;
    }

    // Store the valid precommit
    received_precommits_[precommit.validator_id()] = precommit;
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Received valid precommit from {} for block {}. Total precommits: {}", 
             precommit.validator_id(), precommit.block_hash().substr(0, 8), received_precommits_.size());

    // Check for quorum
    if (received_precommits_.size() >= get_quorum_size()) {
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Quorum of precommits reached for height {} round {}", current_height_, current_round_);
        
        // STATE TRANSITION: COMMIT
        // Quorum of precommits detected. This node transitions to COMMIT state, indicating
        // that enough validators have committed to a block. The block is now ready for
        // finalization (ledger persistence). NodeApp will check this via check_precommit_quorum().
        current_state_ = BftState::COMMIT;

        // Log the finalized block hash if available
        auto quorum_hash = get_quorum_block_hash_from_precommits();
        if (quorum_hash.has_value()) {
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                     "Block finalized at height {} round {} with hash {}",
                     current_height_, current_round_, chrono_util::bytes_to_hex(*quorum_hash).substr(0, 8));
        }

        // Finalization is handled by NodeApp via check_precommit_quorum() and get_finalized_block_hash()
        return std::nullopt;
    }
    return std::nullopt;
}

/**
 * @brief Handles an incoming NewRound message.
 *
 * STATE TRANSITION: This handler transitions to NEW_ROUND state, signaling the start of
 * a new consensus round. It resets vote collections and may unlock previously locked blocks
 * if a Proof-of-Lock (POL) from a higher round is provided.
 *
 * FLOW:
 * 1. Validate sender is a known validator
 * 2. Check round progression (reject old rounds)
 * 3. Verify sender is the leader for this height/round (deterministic leader selection)
 * 4. Check for POL (Proof-of-Lock): If new_round carries evidence of a higher round
 *    precommit quorum, unlock any previously locked block
 * 5. Update to new height/round, clear prevote/precommit collections
 * 6. Transition to NEW_ROUND state
 * 7. (TODO) If a block proposal is attached, validate and potentially broadcast Prevote
 *
 * Processes a `NewRound` message, which signals the start of a new consensus round.
 * It validates the sender and ensures the new round is not from the past. If valid,
 * the gadget updates its internal state to the new height and round, clearing any
 * votes from the previous round.
 *
 * @param new_round The NewRound message to handle.
 * @return A `Prevote` message if this node decides to vote for the new proposal, otherwise `std::nullopt`.
 *         (Note: Currently returns nullopt as block proposal/validation logic is a TODO).
 */
std::optional<chronos::bft::Prevote> BftGadget::handle_new_round(const chronos::bft::NewRound& new_round) {
    // 1. Validator check
    if (!is_validator(new_round.validator_id())) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received new round from unknown validator: {}", new_round.validator_id());
        return std::nullopt;
    }
    
    // 2. Round progression check
    if (new_round.height() < current_height_ || (new_round.height() == current_height_ && new_round.round() < current_round_)) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Received old new round message: {}/{} vs current {}/{}",
                 new_round.height(), new_round.round(), current_height_, current_round_);
        return std::nullopt;
    }
    
    // 3. Leader verification (check if sender is the leader for this round)
    // NOTE: We use current consensus time as seed. In production, this should come from PoT.
    std::string expected_leader = get_leader_for_round(0, new_round.height(), new_round.round());
    if (new_round.validator_id() != expected_leader) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, 
                 "Received new round from non-leader: {} but leader is {} for {}/{}",
                 new_round.validator_id(), expected_leader, new_round.height(), new_round.round());
        return std::nullopt;
    }

    // Advance to new round and clear previous votes
    current_height_ = new_round.height();
    current_round_ = new_round.round();
    
    // STATE TRANSITION: NEW_ROUND
    // A valid NewRound message from the leader transitions us to NEW_ROUND state.
    // We clear all accumulated votes and prepare to receive a new block proposal.
    current_state_ = BftState::NEW_ROUND;
    
    // Reset round start time for timeout tracking
    round_start_time_ = std::chrono::steady_clock::now();
    
    clear_proposed_block();
    received_prevotes_.clear();
    received_precommits_.clear();

    // POL (Proof-of-Lock) logic: Unlock block only if moving to a higher round
    // This ensures safety - once locked, we remain committed unless we see proof
    // that others moved to a higher round
    if (locked_block_.has_value()) {
        if (new_round.round() > locked_round_) {
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                     "Unlocking block from round {} due to new round {} (POL)",
                     locked_round_, new_round.round());
            locked_block_ = std::nullopt;
            locked_round_ = 0;
        } else {
            LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
                     "Keeping block locked from round {} (new round {} not higher)",
                     locked_round_, new_round.round());
        }
    }

    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Advanced to new round {}/{} from leader {}", 
             current_height_, current_round_, new_round.validator_id());

    // TODO: If this node is the leader, propose a block. If not, wait for proposal.
    // For now, return an empty optional.
    return std::nullopt;
}

/**
 * @brief Deterministically selects a leader for a given height and round.
 *
 * This method implements a deterministic leader selection algorithm. It uses a seed
 * composed of the consensus time, height, and round. The seed is hashed with BLAKE3,
 * and the resulting hash is used to select a leader from the sorted validator set.
 * This ensures all nodes agree on the leader for a specific round.
 *
 * @param consensus_time A timestamp or value contributing to the seed.
 * @param height The consensus block height.
 * @param round The consensus round number.
 * @return The identifier of the selected leader validator.
 * @throws std::runtime_error if the validator set is empty or the hash is too short.
 */
std::string BftGadget::get_leader_for_round(uint64_t consensus_time, uint64_t height, uint32_t round) const {
    if (validators_.empty()) {
        throw std::runtime_error("Cannot select leader from an empty validator set.");
    }

    // 1. Create a deterministic seed from the inputs
    std::string seed_str = std::to_string(consensus_time) +
                           std::to_string(height) +
                           std::to_string(round);
    
    // 2. Hash the seed using BLAKE3
    chrono_util::Bytes seed_bytes = chrono_util::string_to_bytes(seed_str);
    chrono_util::Bytes hash_output = chrono_crypto::blake3(seed_bytes);

    // 3. Convert a portion of the hash to a number to use as an index
    // We'll take the first 8 bytes of the hash for this.
    uint64_t hash_val = 0;
    if (hash_output.size() >= 8) {
        // Interpret first 8 bytes in little-endian for cross-platform consistency
        const uint8_t* p = hash_output.data();
        hash_val = static_cast<uint64_t>(p[0]) |
                   (static_cast<uint64_t>(p[1]) << 8) |
                   (static_cast<uint64_t>(p[2]) << 16) |
                   (static_cast<uint64_t>(p[3]) << 24) |
                   (static_cast<uint64_t>(p[4]) << 32) |
                   (static_cast<uint64_t>(p[5]) << 40) |
                   (static_cast<uint64_t>(p[6]) << 48) |
                   (static_cast<uint64_t>(p[7]) << 56);
    } else {
        // Handle case where hash_output is too short (should not happen with BLAKE3)
        // For now, use a fallback, or throw an error.
        throw std::runtime_error("BLAKE3 hash output too short for leader selection.");
    }

    // 4. Determine leader index
    size_t num_validators = validators_.size();
    size_t leader_index = hash_val % num_validators;

    // 5. Select leader from the sorted validator set
    std::vector<std::string> sorted_validators(validators_.begin(), validators_.end());
    return sorted_validators[leader_index];
}

/**
 * @brief Sets the proposed block for the current round.
 *
 * This function stores the block that has been proposed for the current consensus round.
 * This block will be the subject of prevote and precommit messages.
 *
 * @param proposed_block The block being proposed.
 */
void BftGadget::set_proposed_block(const chrono_ledger::Block& proposed_block) {
    current_proposal_ = proposed_block;
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "BftGadget: Proposed block set for height {} round {}", proposed_block.height, current_round_);
}

/**
 * @brief Clears the currently proposed block.
 *
 * This is typically called when a new round begins, to discard the proposal from the
 * previous round.
 */
void BftGadget::clear_proposed_block() {
    current_proposal_ = std::nullopt;
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "BftGadget: Proposed block cleared.");
}

/**
 * @brief Checks if a block has been proposed for a specific height and round.
 *
 * This method verifies if the gadget is currently holding a proposed block that matches
 * the given height and round.
 *
 * @param height The block height to check.
 * @param round The round number to check.
 * @return true if a matching block is currently proposed, false otherwise.
 */
bool BftGadget::has_proposed_for_round(uint64_t height, uint32_t round) const {
    return current_proposal_.has_value() && current_proposal_->height == height && current_round_ == round;
}

/**
 * @brief Checks if a quorum of prevotes has been reached for a specific block hash.
 *
 * Iterates through all received prevotes and counts how many validators have voted
 * for the given block hash. Returns true if the count meets or exceeds the quorum threshold.
 *
 * @param block_hash The block hash to check for prevote quorum.
 * @return true if a quorum of prevotes for this block hash exists, false otherwise.
 */
bool BftGadget::check_prevote_quorum_for_block_hash(const chrono_util::Bytes& block_hash) const {
    size_t count = 0;
    
    for (const auto& [validator_id, prevote] : received_prevotes_) {
        // Convert prevote block_hash string to Bytes for comparison
        chrono_util::Bytes prevote_hash = chrono_util::hex_to_bytes(prevote.block_hash());
        
        if (prevote_hash == block_hash) {
            count++;
        }
    }
    
    LOG_DEBUG(chrono_util::LogCategory::CONSENSUS, 
              "check_prevote_quorum_for_block_hash: {} votes for block (quorum needed: {})",
              count, get_quorum_size());
    
    return count >= get_quorum_size();
}

/**
 * @brief Retrieves the block hash that has reached a prevote quorum.
 *
 * Analyzes all received prevotes and determines if a single block hash has received
 * enough votes to form a quorum. If multiple block hashes have significant votes but
 * none reach quorum, returns nullopt.
 *
 * @return An optional containing the block hash if a quorum is found, nullopt otherwise.
 */
std::optional<chrono_util::Bytes> BftGadget::get_quorum_block_hash_from_prevotes() const {
    // Count votes per block hash
    std::map<chrono_util::Bytes, size_t> block_hash_votes;
    
    for (const auto& [validator_id, prevote] : received_prevotes_) {
        chrono_util::Bytes prevote_hash = chrono_util::hex_to_bytes(prevote.block_hash());
        block_hash_votes[prevote_hash]++;
    }
    
    // Find the block hash with a quorum
    for (const auto& [block_hash, vote_count] : block_hash_votes) {
        if (vote_count >= get_quorum_size()) {
            LOG_INFO(chrono_util::LogCategory::CONSENSUS,
                     "Prevote quorum reached for block hash with {} votes",
                     vote_count);
            return block_hash;
        }
    }
    
    LOG_DEBUG(chrono_util::LogCategory::CONSENSUS,
              "No prevote quorum found. Received {} prevotes",
              received_prevotes_.size());
    return std::nullopt;
}

/**
 * @brief Retrieves the block hash that has reached a precommit quorum.
 *
 * Similar to get_quorum_block_hash_from_prevotes(), this method checks all received
 * precommits and returns the hash of the block that has achieved a quorum of precommits.
 * This indicates the block is ready for finalization.
 *
 * @return An optional containing the block hash if a quorum is found, nullopt otherwise.
 */
std::optional<chrono_util::Bytes> BftGadget::get_quorum_block_hash_from_precommits() const {
    // Count votes per block hash
    std::map<chrono_util::Bytes, size_t> block_hash_votes;
    
    for (const auto& [validator_id, precommit] : received_precommits_) {
        chrono_util::Bytes precommit_hash = chrono_util::hex_to_bytes(precommit.block_hash());
        block_hash_votes[precommit_hash]++;
    }
    
    // Find the block hash with a quorum
    for (const auto& [block_hash, vote_count] : block_hash_votes) {
        if (vote_count >= get_quorum_size()) {
            LOG_INFO(chrono_util::LogCategory::CONSENSUS,
                     "Precommit quorum reached for block hash with {} votes",
                     vote_count);
            return block_hash;
        }
    }
    
    LOG_DEBUG(chrono_util::LogCategory::CONSENSUS,
              "No precommit quorum found. Received {} precommits",
              received_precommits_.size());
    return std::nullopt;
}

/**
 * @brief Checks if a precommit quorum has been reached for a specific height and round.
 *
 * This public method wraps the private get_quorum_block_hash_from_precommits() to provide
 * a simple boolean check. It's useful for the consensus main loop to determine if it's time
 * to finalize a block.
 *
 * @param height The block height to check (should match current height for validity).
 * @param round The consensus round to check (should match current round for validity).
 * @return true if a precommit quorum exists, false otherwise.
 */
bool BftGadget::check_precommit_quorum(uint64_t height, uint32_t round) const {
    // Validate that we're checking for the correct height and round
    if (height != current_height_ || round != current_round_) {
        LOG_DEBUG(chrono_util::LogCategory::CONSENSUS,
                  "check_precommit_quorum called for {}/{}, but current is {}/{}. Not checking.",
                  height, round, current_height_, current_round_);
        return false;
    }
    
    return get_quorum_block_hash_from_precommits().has_value();
}

/**
 * @brief Retrieves the block hash that has achieved a precommit quorum for finalization.
 *
 * This public method returns the block hash that has reached a precommit quorum,
 * allowing the consensus engine to know which block should be finalized.
 *
 * @return An optional containing the block hash if a quorum exists, nullopt otherwise.
 */
std::optional<chrono_util::Bytes> BftGadget::get_finalized_block_hash() const {
    return get_quorum_block_hash_from_precommits();
}

/**
 * @brief Validates that a block hash is not empty.
 *
 * This helper checks that the block hash field is not empty, which would indicate
 * an invalid or malformed message.
 *
 * @param block_hash The block hash string to validate.
 * @return true if block_hash is non-empty, false otherwise.
 */
bool BftGadget::is_valid_block_hash(const std::string& block_hash) const {
    return !block_hash.empty();
}

/**
 * @brief Checks if a prevote from a specific validator has already been received.
 *
 * This prevents accepting duplicate prevote messages from the same validator for
 * the current round, which would be a protocol violation.
 *
 * @param validator_id The validator ID to check.
 * @return true if a prevote from this validator exists, false otherwise.
 */
bool BftGadget::has_prevote_from_validator(const std::string& validator_id) const {
    return received_prevotes_.count(validator_id) > 0;
}

/**
 * @brief Checks if a precommit from a specific validator has already been received.
 *
 * This prevents accepting duplicate precommit messages from the same validator for
 * the current round, which would be a protocol violation.
 *
 * @param validator_id The validator ID to check.
 * @return true if a precommit from this validator exists, false otherwise.
 */
bool BftGadget::has_precommit_from_validator(const std::string& validator_id) const {
    return received_precommits_.count(validator_id) > 0;
}

/**
 * @brief Creates a signed Prevote message for a specific block hash.
 *
 * Constructs a Prevote protobuf message with the current height, round, and block hash,
 * then signs it using the configured signer. The signature ensures message authenticity
 * and prevents message tampering.
 *
 * @param block_hash The hash of the block to prevote for.
 * @return A signed Prevote message, or nullopt if no signer is configured.
 */
std::optional<chronos::bft::Prevote> BftGadget::create_prevote(const chrono_util::Bytes& block_hash) {
    if (!signer_) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Cannot create prevote: no signer configured");
        return std::nullopt;
    }

    chronos::bft::Prevote prevote;
    prevote.set_height(current_height_);
    prevote.set_round(current_round_);
    prevote.set_block_hash(chrono_util::bytes_to_hex(block_hash));
    prevote.set_validator_id(my_validator_id_);

    // Serialize the prevote for signing (without signature field)
    std::string serialized = prevote.SerializeAsString();
    chrono_util::Bytes message_bytes(serialized.begin(), serialized.end());

    // Sign the message
    chrono_util::Bytes signature = signer_->sign(message_bytes);

    // Set the signature in the protobuf
    auto* sig_proto = prevote.mutable_signature();
    sig_proto->set_data(std::string(signature.begin(), signature.end()));

    LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
             "Created signed prevote for block {} at height {} round {}",
             chrono_util::bytes_to_hex(block_hash).substr(0, 8), current_height_, current_round_);

    return prevote;
}

/**
 * @brief Creates a signed Precommit message for a specific block hash.
 *
 * Constructs a Precommit protobuf message with the current height, round, and block hash,
 * then signs it using the configured signer. A precommit represents a stronger commitment
 * than a prevote and indicates readiness to finalize the block.
 *
 * @param block_hash The hash of the block to precommit for.
 * @return A signed Precommit message, or nullopt if no signer is configured.
 */
std::optional<chronos::bft::Precommit> BftGadget::create_precommit(const chrono_util::Bytes& block_hash) {
    if (!signer_) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Cannot create precommit: no signer configured");
        return std::nullopt;
    }

    chronos::bft::Precommit precommit;
    precommit.set_height(current_height_);
    precommit.set_round(current_round_);
    precommit.set_block_hash(chrono_util::bytes_to_hex(block_hash));
    precommit.set_validator_id(my_validator_id_);

    // Serialize the precommit for signing (without signature field)
    std::string serialized = precommit.SerializeAsString();
    chrono_util::Bytes message_bytes(serialized.begin(), serialized.end());

    // Sign the message
    chrono_util::Bytes signature = signer_->sign(message_bytes);

    // Set the signature in the protobuf
    auto* sig_proto = precommit.mutable_signature();
    sig_proto->set_data(std::string(signature.begin(), signature.end()));

    LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
             "Created signed precommit for block {} at height {} round {}",
             chrono_util::bytes_to_hex(block_hash).substr(0, 8), current_height_, current_round_);

    return precommit;
}

/**
 * @brief Checks if the current consensus round has exceeded its timeout threshold.
 *
 * This method calculates the elapsed time since the round started and compares it
 * to the configured timeout duration. A timed-out round indicates that consensus
 * has stalled and the validators should move to the next round to maintain liveness.
 *
 * TIMEOUT DETECTION:
 * - Measures elapsed time since round_start_time_
 * - Compares against round_timeout_ms_ threshold
 * - Returns true if timeout exceeded, false otherwise
 *
 * @return true if the current round has timed out, false otherwise.
 */
bool BftGadget::is_round_timed_out() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - round_start_time_).count();
    return elapsed_ms >= round_timeout_ms_;
}

/**
 * @brief Advances the consensus to the next round after a timeout or stall.
 *
 * This method handles the transition to a new consensus round when the current round
 * fails to reach consensus within the timeout period. It preserves safety by maintaining
 * the locked block (if any) while resetting vote collections and incrementing the round.
 *
 * ROUND ADVANCEMENT FLOW:
 * 1. Increment current_round_
 * 2. Clear received prevotes and precommits (stale votes)
 * 3. Clear current proposal (will need new proposal for new round)
 * 4. Reset round start time to now
 * 5. Transition to NEW_ROUND state
 * 6. Keep locked_block_ and locked_round_ (POL safety guarantee)
 * 7. Create and return NewRound message for broadcasting
 *
 * POL PRESERVATION:
 * The locked block is NOT cleared during round advancement. This ensures that if a validator
 * locked a block in round R, they remain committed to it in round R+1 unless a higher POL
 * is presented via a NewRound message from another validator.
 *
 * @return A NewRound message to broadcast, signaling the round change to all validators.
 */
chronos::bft::NewRound BftGadget::advance_to_next_round() {
    // Increment round counter
    current_round_++;
    
    // Clear accumulated votes (they're stale for the new round)
    received_prevotes_.clear();
    received_precommits_.clear();
    
    // Clear current proposal (need fresh proposal for new round)
    clear_proposed_block();
    
    // Reset round start time
    round_start_time_ = std::chrono::steady_clock::now();
    
    // STATE TRANSITION: NEW_ROUND
    // Timeout has forced us to move to a new round. We transition to NEW_ROUND state
    // and wait for the new leader to propose a block.
    current_state_ = BftState::NEW_ROUND;
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
             "Advanced to next round due to timeout. Height: {}, New Round: {}", 
             current_height_, current_round_);
    
    // Create NewRound message to broadcast
    chronos::bft::NewRound new_round;
    new_round.set_height(current_height_);
    new_round.set_round(current_round_);
    new_round.set_validator_id(my_validator_id_);
    
    // TODO: In production, attach POL (Proof-of-Lock) if we have a locked block
    // This would include precommit signatures from the round where the block was locked
    
    return new_round;
}

/**
 * @brief Advances to the next block height after successful block finalization.
 *
 * This method performs a complete state reset for the next consensus height:
 * 1. Increments the block height
 * 2. Resets round counter to 0
 * 3. Clears all accumulated votes (prevotes and precommits)
 * 4. Clears the current proposal
 * 5. Resets the round timeout timer
 * 6. Transitions to INITIAL state for the new height
 *
 * The locked_block_ is intentionally NOT cleared to support the Proof-of-Lock (POL)
 * mechanism in Byzantine Fault Tolerant consensus, allowing validators to commit
 * on the same block across height transitions if necessary.
 *
 * @param new_height The new block height to transition to.
 */
void BftGadget::advance_to_next_height(uint64_t new_height) {
    // Increment height
    current_height_ = new_height;
    
    // Reset round to 0 for new height
    current_round_ = 0;
    
    // Clear all votes accumulated at previous height (they're invalid for new height)
    received_prevotes_.clear();
    received_precommits_.clear();
    
    // Clear the current proposal (leader will propose fresh block for new height)
    clear_proposed_block();
    
    // Reset round timeout timer for the new height
    round_start_time_ = std::chrono::steady_clock::now();
    
    // STATE TRANSITION: INITIAL
    // We transition to INITIAL state to await block proposal from the leader
    current_state_ = BftState::INITIAL;
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, 
             "Advanced to next height after finalization. New Height: {}, Reset to Round 0",
             current_height_);
    
    // NOTE: locked_block_ is intentionally maintained across height transitions
    // This preserves the Proof-of-Lock (POL) mechanism in BFT consensus
}

} // namespace chrono_consensus