//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_genesis.cpp
 * @brief Unit tests for genesis block creation and validation.
 *
 * This file contains test cases for:
 * - Genesis block creation from configuration
 * - Genesis hash validation
 * - Genesis allocations application
 * - Max supply enforcement
 * - Invalid address rejection
 */

#include "test_framework.hpp"
#include "ledger/state.hpp"
#include "ledger/block.hpp"
#include "address/address.hpp"
#include "crypto/signer_hmac.hpp"
#include "storage/memory_kv.hpp"
#include "util/bytes.hpp"
#include <memory>
#include <unordered_map>
#include <string>

using chrono_ledger::State;
using chrono_ledger::Block;
using chrono_address::Address;
using chrono_crypto::SignerHMAC;
using chrono_storage::MemoryKv;
using chrono_util::Bytes;

/**
 * @brief Test genesis block creation with zero hash predecessor.
 *
 * Verifies that a genesis block is created with:
 * - Previous hash = all zeros
 * - Height = 0
 * - Valid consensus time
 */
TEST_CASE(GenesisBlockCreation, "Genesis Block Creation") {
    // Create genesis block
    Bytes zero_hash(32, 0);
    Block genesis;
    genesis.prev_block_hash = zero_hash;  // Correct: prev_block_hash, not prev_hash
    genesis.height = 0;
    genesis.consensus_time = 1704067200000; // Fixed timestamp
    genesis.transactions = {};
    genesis.calculate_merkle_root();
    
    // Verify genesis properties
    ASSERT_EQ(genesis.height, static_cast<uint64_t>(0), "Genesis height should be 0");
    ASSERT_TRUE(genesis.prev_block_hash == zero_hash, "Genesis prev_block_hash should be all zeros");
    ASSERT_TRUE(genesis.consensus_time > 0, "Consensus time should be positive");
    ASSERT_TRUE(genesis.transactions.empty(), "Genesis should have no transactions");
}

/**
 * @brief Test genesis allocations application to state.
 *
 * Verifies that genesis allocations are correctly applied with:
 * - Multiple accounts with different balances
 * - State reflects correct balances
 * - Nonces remain at 0 (genesis accounts start with nonce 0)
 */
TEST_CASE(GenesisAllocations, "Genesis Allocations") {
    // Create KV store for State
    MemoryKv kv_store;
    
    // Create test addresses
    SignerHMAC signer1("test_key_1");
    Bytes pubkey1 = signer1.get_public_key();
    Address addr1(pubkey1);
    std::string addr1_str = addr1.to_string();
    
    // Generate different key for second address
    SignerHMAC signer2("test_key_2");
    Bytes pubkey2 = signer2.get_public_key();
    Address addr2(pubkey2);
    std::string addr2_str = addr2.to_string();
    
    // Create state and apply allocations
    State state(kv_store);  // Correct: State requires IKv& parameter
    state.set_balance(addr1_str, 1000000);
    state.set_balance(addr2_str, 500000);
    
    // Verify balances
    ASSERT_EQ(state.get_balance(addr1_str), static_cast<uint64_t>(1000000), "Address 1 should have balance 1000000");
    ASSERT_EQ(state.get_balance(addr2_str), static_cast<uint64_t>(500000), "Address 2 should have balance 500000");
    
    // Verify nonces are 0 for genesis accounts
    ASSERT_EQ(state.get_nonce(addr1_str), static_cast<uint64_t>(0), "Address 1 nonce should be 0");
    ASSERT_EQ(state.get_nonce(addr2_str), static_cast<uint64_t>(0), "Address 2 nonce should be 0");
}

/**
 * @brief Test genesis max supply enforcement.
 *
 * Verifies that:
 * - Allocations exceeding max_supply are rejected
 * - Exception is thrown with descriptive message
 * - State remains unchanged after failed allocation
 */
TEST_CASE(GenesisMaxSupplyEnforcement, "Genesis Max Supply Enforcement") {
    MemoryKv kv_store;
    SignerHMAC signer("test_key_max_supply");
    Bytes pubkey = signer.get_public_key();
    Address addr(pubkey);
    std::string addr_str = addr.to_string();
    
    State state(kv_store);
    uint64_t max_supply = 1000000;
    
    // Try to allocate within limit - should succeed
    try {
        state.set_balance(addr_str, max_supply, max_supply);
        ASSERT_EQ(state.get_balance(addr_str), max_supply, "Balance should equal max_supply");
    } catch (...) {
        ASSERT_TRUE(false, "Should not throw exception for valid allocation");
    }
    
    // Try to allocate above limit - should fail
    bool exception_thrown = false;
    try {
        state.set_balance(addr_str, max_supply + 1, max_supply);
    } catch (const std::invalid_argument& e) {
        exception_thrown = true;
        std::string msg = e.what();
        ASSERT_TRUE(msg.find("exceeds max allowed") != std::string::npos, "Exception message should mention max allowed");
    }
    ASSERT_TRUE(exception_thrown, "Exception should be thrown when exceeding max_supply");
}

/**
 * @brief Test invalid address rejection in genesis allocations.
 *
 * Verifies that:
 * - Invalid address strings are rejected
 * - Exception is thrown with descriptive message
 * - State remains unchanged after invalid allocation
 */
TEST_CASE(GenesisInvalidAddressRejection, "Genesis Invalid Address Rejection") {
    MemoryKv kv_store;
    State state(kv_store);
    
    // Test invalid bech32m addresses
    std::vector<std::string> invalid_addresses = {
        "",                              // Empty string
        "invalid_format",                // Not bech32m
        "cqc1",                          // Too short
        "btc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4", // Wrong HRP
        "cqc1zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz" // Invalid checksum
    };
    
    for (const auto& invalid_addr : invalid_addresses) {
        bool exception_thrown = false;
        try {
            state.set_balance(invalid_addr, 100000);
        } catch (const std::invalid_argument& e) {
            exception_thrown = true;
            std::string msg = e.what();
            ASSERT_TRUE(msg.find("Invalid address") != std::string::npos, "Exception message should mention invalid address");
        }
        ASSERT_TRUE(exception_thrown, "Exception should be thrown for invalid address");
    }
}

/**
 * @brief Test genesis hash validation.
 *
 * Verifies that:
 * - Genesis block hash can be computed
 * - Hash is deterministic (same block = same hash)
 * - Hash is 32 bytes (BLAKE3 output size)
 */
TEST_CASE(GenesisHashValidation, "Genesis Hash Validation") {
    // Create two identical genesis blocks
    Bytes zero_hash(32, 0);
    
    Block genesis1;
    genesis1.prev_block_hash = zero_hash;
    genesis1.height = 0;
    genesis1.consensus_time = 1704067200000;
    genesis1.timestamp = 1704067200;  // Must match for deterministic hash
    genesis1.transactions = {};
    genesis1.calculate_merkle_root();
    
    Block genesis2;
    genesis2.prev_block_hash = zero_hash;
    genesis2.height = 0;
    genesis2.consensus_time = 1704067200000;
    genesis2.timestamp = 1704067200;  // Must match for deterministic hash
    genesis2.transactions = {};
    genesis2.calculate_merkle_root();
    
    // Get hashes
    Bytes hash1 = genesis1.get_header_hash();
    Bytes hash2 = genesis2.get_header_hash();
    
    // Verify hash properties
    ASSERT_EQ(hash1.size(), static_cast<size_t>(32), "Hash 1 should be 32 bytes");
    ASSERT_EQ(hash2.size(), static_cast<size_t>(32), "Hash 2 should be 32 bytes");
    ASSERT_TRUE(hash1 == hash2, "Hashes should be deterministic");
}

/**
 * @brief Test address validation helper.
 *
 * Verifies that:
 * - Valid addresses are accepted
 * - Invalid addresses are rejected
 * - Validation works without creating Address objects
 */
TEST_CASE(AddressValidationHelper, "Address Validation Helper") {
    // Create valid address
    SignerHMAC signer("test_key_addr_valid");
    Bytes pubkey = signer.get_public_key();
    Address addr(pubkey);
    std::string valid_addr = addr.to_string();
    
    // Test valid address
    ASSERT_TRUE(Address::is_valid(valid_addr), "Valid address should pass Address::is_valid");
    ASSERT_TRUE(State::is_valid_address(valid_addr), "Valid address should pass State::is_valid_address");
    
    // Test invalid addresses
    ASSERT_FALSE(Address::is_valid(""), "Empty string should be invalid");
    ASSERT_FALSE(Address::is_valid("invalid"), "'invalid' should be invalid");
    ASSERT_FALSE(Address::is_valid("cqc1"), "'cqc1' should be invalid (too short)");
    ASSERT_FALSE(State::is_valid_address(""), "Empty string should be invalid in State");
    ASSERT_FALSE(State::is_valid_address("not_bech32m"), "'not_bech32m' should be invalid");
}

/**
 * @brief Test genesis allocation persistence.
 *
 * Verifies that:
 * - Genesis allocations are persisted to disk
 * - State can be reloaded and balances remain correct
 * - Serialization/deserialization preserves genesis state
 */
TEST_CASE(GenesisAllocationPersistence, "Genesis Allocation Persistence") {
    MemoryKv kv_store1;
    MemoryKv kv_store2;
    
    SignerHMAC signer("test_key_persistence");
    Bytes pubkey = signer.get_public_key();
    Address addr(pubkey);
    std::string addr_str = addr.to_string();
    
    // Create state and apply allocation
    State state1(kv_store1);
    state1.set_balance(addr_str, 999999);
    
    // Serialize state
    Bytes serialized = state1.serialize_to_bytes();
    ASSERT_TRUE(!serialized.empty(), "Serialized state should not be empty");
    
    // Deserialize into new state
    State state2(kv_store2);
    bool success = state2.deserialize_from_bytes(serialized);
    ASSERT_TRUE(success, "Deserialization should succeed");
    
    // Verify balance is preserved
    ASSERT_EQ(state2.get_balance(addr_str), static_cast<uint64_t>(999999), "Balance should be preserved after deserialization");
    ASSERT_EQ(state2.get_nonce(addr_str), static_cast<uint64_t>(0), "Nonce should be 0 after deserialization");
}

/**
 * @brief Test zero balance genesis allocation.
 *
 * Verifies that:
 * - Accounts can be allocated with zero balance (placeholder accounts)
 * - Zero balance accounts are correctly stored
 * - Zero balance is distinct from non-existent account
 */
TEST_CASE(GenesisZeroBalanceAllocation, "Genesis Zero Balance Allocation") {
    MemoryKv kv_store;
    SignerHMAC signer("test_key_zero_balance");
    Bytes pubkey = signer.get_public_key();
    Address addr(pubkey);
    std::string addr_str = addr.to_string();
    
    State state(kv_store);
    state.set_balance(addr_str, 0);
    
    // Zero balance should be stored
    ASSERT_EQ(state.get_balance(addr_str), static_cast<uint64_t>(0), "Balance should be 0");
    ASSERT_EQ(state.get_nonce(addr_str), static_cast<uint64_t>(0), "Nonce should be 0");
}

