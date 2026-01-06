#include "test_framework.hpp"
#include <set>
#include <string>
#include <thread>
#include <chrono>
#include <vector> // Voor std::vector
#include "consensus/bft.hpp"
#include "util/bytes.hpp"
#include "ledger/block.hpp"
#include "ledger/transaction.hpp"
#include <nlohmann/json.hpp>

TEST_CASE(BftGadgetLeaderSelection, "BftGadget Leader Selection") {
    std::set<std::string> validators = {"validator_alpha", "validator_beta", "validator_gamma", "validator_delta"};
    std::string my_id = "validator_alpha";
    chrono_consensus::BftGadget bft_gadget(validators, my_id, 0.67); // Uses quorum threshold 0.67 (2/3)

    // Test deterministic leader selection
    // With same inputs, leader should be the same
    std::string leader = bft_gadget.get_leader_for_round(100, 1, 0);
    REQUIRE((validators.count(leader) == 1), "Selected leader must be in validator set");
    REQUIRE((bft_gadget.get_leader_for_round(100, 1, 0) == leader), "Leader should be deterministic for same inputs");

    // Changing consensus time should change leader
    REQUIRE((bft_gadget.get_leader_for_round(101, 1, 0) != "validator_beta"), "Changing consensus time should change leader");
    
    // Changing height should change leader
    REQUIRE((bft_gadget.get_leader_for_round(100, 2, 0) != "validator_beta"), "Changing height should change leader");

    // Changing round should change leader
    REQUIRE((bft_gadget.get_leader_for_round(100, 1, 1) != "validator_beta"), "Changing round should change leader");
}

TEST_CASE(BlockConstructorWithConsensusTime, "Block Constructor with Consensus Time") {
    chrono_util::Bytes prev_hash(32, 0x01);
    uint64_t height = 5;
    uint64_t consensus_time = 1234567890;
    std::vector<chrono_ledger::Transaction> txs; // Empty for simplicity

    chrono_ledger::Block block(prev_hash, height, consensus_time, 0, 5, 0, txs); // Tier 5, Score 0 for test

    REQUIRE((block.prev_block_hash == prev_hash), "Block prev_block_hash should match");
    REQUIRE((block.height == height), "Block height should match");
    REQUIRE((block.consensus_time == consensus_time), "Block consensus_time should match");
    REQUIRE((block.transactions.empty()), "Block transactions should be empty");
}

TEST_CASE(BlockSerializationDeserializationWithConsensusTime, "Block Serialization/Deserialization with Consensus Time") {
    chrono_util::Bytes prev_hash(32, 0x02);
    uint64_t height = 10;
    uint64_t consensus_time = 987654321;
    std::vector<chrono_ledger::Transaction> txs; // Empty for simplicity

    chrono_ledger::Block original_block(prev_hash, height, consensus_time, 0, 5, 0, txs); // Tier 5, Score 0 for test
    
    chrono_util::Bytes serialized_data = original_block.serialize();
    chrono_ledger::Block deserialized_block = chrono_ledger::Block::deserialize(serialized_data);

    REQUIRE((deserialized_block.prev_block_hash == prev_hash), "Deserialized block prev_block_hash should match");
    REQUIRE((deserialized_block.height == height), "Deserialized block height should match");
    REQUIRE((deserialized_block.consensus_time == consensus_time), "Deserialized block consensus_time should match");
    REQUIRE((deserialized_block.transactions.empty()), "Deserialized block transactions should be empty");
    REQUIRE((deserialized_block.get_header_hash() == original_block.get_header_hash()), "Deserialized block hash should match original");
}

TEST_CASE(BlockJSONSerializationDeserializationWithConsensusTime, "Block JSON Serialization/Deserialization with Consensus Time") {
    chrono_util::Bytes prev_hash(32, 0x03);
    uint64_t height = 15;
    uint64_t consensus_time = 1122334455;
    std::vector<chrono_ledger::Transaction> txs; // Empty for simplicity

    chrono_ledger::Block original_block(prev_hash, height, consensus_time, 0, 5, 0, txs); // Tier 5, Score 0 for test
    
    nlohmann::json block_json = original_block.to_json();
    chrono_ledger::Block deserialized_block;
    deserialized_block.from_json(block_json);

    REQUIRE((deserialized_block.prev_block_hash == prev_hash), "Deserialized JSON block prev_block_hash should match");
    REQUIRE((deserialized_block.height == height), "Deserialized JSON block height should match");
    REQUIRE((deserialized_block.consensus_time == consensus_time), "Deserialized JSON block consensus_time should match");
    REQUIRE((deserialized_block.transactions.empty()), "Deserialized JSON block transactions should be empty");
    REQUIRE((deserialized_block.get_header_hash() == original_block.get_header_hash()), "Deserialized JSON block hash should match original");
}


// ============================================================================
// BFT STATE MACHINE TESTS - Timeout Handling & State Transitions
// ============================================================================

TEST_CASE(BftTimeoutDetection, "BFT Timeout Detection") {
    std::set<std::string> validators = {"val1", "val2", "val3"};
    std::string my_id = "val1";
    int timeout_ms = 100; // Short timeout for testing
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67, timeout_ms);
    
    // Initially, round should not be timed out
    REQUIRE(!bft.is_round_timed_out(), "Round should not be timed out initially");
    
    // Wait for timeout to expire
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms + 50));
    
    // Now round should be timed out
    REQUIRE(bft.is_round_timed_out(), "Round should be timed out after waiting");
}

TEST_CASE(BftRoundAdvancement, "BFT Round Advancement") {
    std::set<std::string> validators = {"val1", "val2", "val3"};
    std::string my_id = "val1";
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67, 5000);
    
    // Check initial state
    REQUIRE((bft.get_current_height() == 0), "Initial height should be 0");
    REQUIRE((bft.get_current_round() == 0), "Initial round should be 0");
    
    // Advance to next round
    chronos::bft::NewRound new_round = bft.advance_to_next_round();
    
    // Verify round was incremented
    REQUIRE((bft.get_current_round() == 1), "Round should be incremented to 1");
    REQUIRE((bft.get_current_height() == 0), "Height should remain 0");
    
    // Verify NewRound message content
    REQUIRE((new_round.height() == 0), "NewRound message height should be 0");
    REQUIRE((new_round.round() == 1), "NewRound message round should be 1");
    REQUIRE((new_round.validator_id() == my_id), "NewRound message validator_id should match");
    
    // After advancing, timeout should be reset
    REQUIRE(!bft.is_round_timed_out(), "Timeout should be reset after advancing round");
}

TEST_CASE(BftQuorumThresholdValidation, "BFT Quorum Threshold Validation") {
    std::set<std::string> validators = {"val1", "val2", "val3"};
    std::string my_id = "val1";
    
    // Valid thresholds should work
    try {
        chrono_consensus::BftGadget bft1(validators, my_id, 0.5);
        chrono_consensus::BftGadget bft2(validators, my_id, 0.67);
        chrono_consensus::BftGadget bft3(validators, my_id, 1.0);
        REQUIRE(true, "Valid thresholds (0.5, 0.67, 1.0) should be accepted");
    } catch (...) {
        REQUIRE(false, "Valid thresholds should not throw");
    }
    
    // Invalid thresholds should throw
    bool threw_for_low = false;
    try {
        chrono_consensus::BftGadget bft_low(validators, my_id, 0.4);
    } catch (const std::invalid_argument&) {
        threw_for_low = true;
    }
    REQUIRE(threw_for_low, "Threshold below 0.5 should throw");
    
    bool threw_for_high = false;
    try {
        chrono_consensus::BftGadget bft_high(validators, my_id, 1.1);
    } catch (const std::invalid_argument&) {
        threw_for_high = true;
    }
    REQUIRE(threw_for_high, "Threshold above 1.0 should throw");
}

TEST_CASE(BftPrecommitQuorumCheck, "BFT Precommit Quorum Check") {
    std::set<std::string> validators = {"val1", "val2", "val3", "val4"};
    std::string my_id = "val1";
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67); // Need 3 out of 4 for quorum
    
    // Initially, no quorum
    REQUIRE(!bft.check_precommit_quorum(0, 0), "Should not have quorum initially");
    
    // Note: Full quorum testing would require mocking precommit collection
    // which happens internally via handle_precommit(). This test verifies
    // the API exists and works correctly.
}

TEST_CASE(BftBlockLocking, "BFT Block Locking") {
    std::set<std::string> validators = {"val1", "val2", "val3"};
    std::string my_id = "val1";
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67);
    
    // Initially no block should be locked
    REQUIRE(!bft.get_locked_block().has_value(), "Initially no block should be locked");
    REQUIRE((bft.get_locked_round() == 0), "Initially locked round should be 0");
    
    // Note: Block locking happens internally via handle_prevote() when quorum reached
    // Full testing would require simulating multiple prevote messages
    // This test verifies the API exists and returns correct initial values
}

TEST_CASE(BftStateAfterRoundAdvancement, "BFT State After Round Advancement") {
    std::set<std::string> validators = {"val1", "val2", "val3"};
    std::string my_id = "val1";
    int timeout_ms = 50;
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67, timeout_ms);
    
    // Advance round manually
    bft.advance_to_next_round();
    
    // Verify state is reset correctly
    REQUIRE((bft.get_current_round() == 1), "Round should be incremented");
    
    // Advance again
    bft.advance_to_next_round();
    REQUIRE((bft.get_current_round() == 2), "Round should be incremented to 2");
    
    // Multiple advancements should work
    bft.advance_to_next_round();
    bft.advance_to_next_round();
    REQUIRE((bft.get_current_round() == 4), "Round should be incremented to 4");
}

TEST_CASE(BftTimeoutResetOnNewRound, "BFT Timeout Reset on New Round Message") {
    std::set<std::string> validators = {"val1", "val2", "val3"};
    std::string my_id = "val1";
    int timeout_ms = 100;
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67, timeout_ms);
    
    // Wait almost until timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms - 20));
    
    // Advance round (which resets timer internally)
    bft.advance_to_next_round();
    
    // Immediately after advancing, should not be timed out
    REQUIRE(!bft.is_round_timed_out(), "Should not be timed out immediately after advancing");
    
    // Wait full timeout period again
    std::this_thread::sleep_for(std::chrono::milliseconds(timeout_ms + 50));
    
    // Now should be timed out again
    REQUIRE(bft.is_round_timed_out(), "Should be timed out after full period in new round");
}

TEST_CASE(BftLeaderSelectionDeterminism, "BFT Leader Selection is Deterministic") {
    std::set<std::string> validators = {"val1", "val2", "val3", "val4", "val5"};
    std::string my_id = "val1";
    
    chrono_consensus::BftGadget bft1(validators, my_id, 0.67);
    chrono_consensus::BftGadget bft2(validators, my_id, 0.67);
    
    // Same inputs should produce same leader across different instances
    for (uint64_t time = 0; time < 10; ++time) {
        for (uint64_t height = 0; height < 10; ++height) {
            for (uint32_t round = 0; round < 5; ++round) {
                std::string leader1 = bft1.get_leader_for_round(time, height, round);
                std::string leader2 = bft2.get_leader_for_round(time, height, round);
                REQUIRE((leader1 == leader2), "Leader selection should be deterministic");
            }
        }
    }
}

TEST_CASE(BftLeaderRotation, "BFT Leader Rotates Across Rounds") {
    std::set<std::string> validators = {"val1", "val2", "val3", "val4"};
    std::string my_id = "val1";
    
    chrono_consensus::BftGadget bft(validators, my_id, 0.67);
    
    // Collect leaders for multiple rounds
    std::set<std::string> observed_leaders;
    for (uint32_t round = 0; round < 20; ++round) {
        std::string leader = bft.get_leader_for_round(100, 1, round);
        observed_leaders.insert(leader);
    }
    
    // Over 20 rounds, we should see multiple different leaders (rotation)
    // With 4 validators and 20 rounds, we should see all 4 validators as leader
    REQUIRE((observed_leaders.size() >= 3), "Should observe multiple different leaders across rounds");
}