/**
 * @file test_protobuf_validation.cpp
 * @brief Tests for protobuf message validation constants and sizes.
 *
 * Verifies that message validation constants are correctly defined for
 * preventing DoS attacks and ensuring message integrity.
 */

#include "test_framework.hpp"
#include "node/node_app.hpp"
#include "util/bytes.hpp"

using namespace chrono_node;
using namespace chrono_util;

TEST_CASE(MessageSizeConstantsValid, "Message Size Limit Constants are Defined") {
    // Verify all size constants are reasonable
    ASSERT_TRUE(NodeApp::MAX_MESSAGE_SIZE > 0, "MAX_MESSAGE_SIZE must be > 0");
    ASSERT_TRUE(NodeApp::MAX_BLOCK_SIZE > 0, "MAX_BLOCK_SIZE must be > 0");
    ASSERT_TRUE(NodeApp::MAX_TRANSACTION_SIZE > 0, "MAX_TRANSACTION_SIZE must be > 0");
}

TEST_CASE(MessageSizeConstantsHierarchy, "Message Size Limits in Correct Hierarchy") {
    // Block size should be less than overall message size
    ASSERT_TRUE(NodeApp::MAX_BLOCK_SIZE < NodeApp::MAX_MESSAGE_SIZE, 
                "Block size should be less than overall message size");
    
    // Transaction size should be less than block size
    ASSERT_TRUE(NodeApp::MAX_TRANSACTION_SIZE < NodeApp::MAX_BLOCK_SIZE,
                "Transaction size should be less than block size");
}

TEST_CASE(HashSizeCorrect, "Hash Size Constant is Blake3 Compatible") {
    // Blake3 produces 32-byte hashes
    ASSERT_EQ(NodeApp::EXPECTED_HASH_SIZE, 32, "Expected hash size should be 32 bytes (Blake3)");
}

TEST_CASE(NodeIDLengthLimits, "Node ID Length Limits are Reasonable") {
    // MIN should be less than MAX
    ASSERT_TRUE(NodeApp::MIN_NODE_ID_LENGTH < NodeApp::MAX_NODE_ID_LENGTH,
                "Min node ID length should be less than max");
    
    // Both should be reasonable (positive and not extreme)
    ASSERT_TRUE(NodeApp::MIN_NODE_ID_LENGTH > 0, "Min node ID length should be positive");
    ASSERT_TRUE(NodeApp::MAX_NODE_ID_LENGTH < 10000, "Max node ID length should be reasonable");
}

TEST_CASE(PortNumberValid, "Port Number Constant is Valid") {
    // Maximum port should be 65535
    ASSERT_EQ(NodeApp::MAX_PORT_NUMBER, 65535, "Max port number should be 65535");
}

TEST_CASE(GetBlocksLimitReasonable, "GetBlocks Limit Prevents DoS") {
    // Limit should be reasonable (not too large, not too small)
    ASSERT_TRUE(NodeApp::MAX_GET_BLOCKS_LIMIT > 0, "GetBlocks limit must be > 0");
    ASSERT_TRUE(NodeApp::MAX_GET_BLOCKS_LIMIT < 10000, "GetBlocks limit should be reasonable");
    
    // Should be small enough that MAX_GET_BLOCKS_LIMIT * MAX_BLOCK_SIZE doesn't overflow
    // With 100 blocks * 5MB = 500MB which is reasonable
}

TEST_CASE(DoSProtectionLimits, "Constants Prevent DoS Attacks") {
    // Verify that message size limits prevent common DoS attacks
    
    // An attacker sending messages at limit shouldn't be able to exhaust memory quickly
    // 10MB message limit is reasonable for a network protocol
    ASSERT_EQ(NodeApp::MAX_MESSAGE_SIZE, 10 * 1024 * 1024, 
              "10MB message limit prevents DoS via large messages");
    
    // Block size limited to 5MB
    ASSERT_EQ(NodeApp::MAX_BLOCK_SIZE, 5 * 1024 * 1024,
              "5MB block limit prevents DoS via oversized blocks");
    
    // Transaction size limited to 100KB
    ASSERT_EQ(NodeApp::MAX_TRANSACTION_SIZE, 100 * 1024,
              "100KB transaction limit prevents DoS via oversized transactions");
}

TEST_CASE(NetworkProtocolLimits, "Network Protocol Parameters are Sensible") {
    // GetBlocks request limit
    ASSERT_EQ(NodeApp::MAX_GET_BLOCKS_LIMIT, 100,
              "100 block limit per GetBlocks prevents excessive requests");
    
    // Node ID format limits
    ASSERT_EQ(NodeApp::MIN_NODE_ID_LENGTH, 10,
              "10 character minimum for node ID ensures reasonable identifiers");
    ASSERT_EQ(NodeApp::MAX_NODE_ID_LENGTH, 4096,
              "4096 character maximum for node ID supports Dilithium hex-encoded public keys");
}

TEST_CASE(ValidationConstantsExist, "All Validation Constants are Defined") {
    // This test verifies that all expected constants exist and are accessible
    volatile size_t constants[] = {
        NodeApp::MAX_MESSAGE_SIZE,
        NodeApp::MAX_BLOCK_SIZE,
        NodeApp::MAX_TRANSACTION_SIZE,
        NodeApp::EXPECTED_HASH_SIZE,
        NodeApp::MAX_GET_BLOCKS_LIMIT,
        NodeApp::MIN_NODE_ID_LENGTH,
        NodeApp::MAX_NODE_ID_LENGTH,
        NodeApp::MAX_PORT_NUMBER
    };
    
    // If we got here, all constants exist
    ASSERT_TRUE(constants[0] > 0, "All validation constants should be defined");
}

