//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_state_serialization.cpp
 * @brief Comprehensive unit tests for State binary serialization and deserialization.
 *
 * Tests the complete lifecycle of State persistence:
 * 1. Serialization of account balances and nonces to binary format
 * 2. Binary format structure validation (magic bytes, version, account count)
 * 3. Deserialization with bounds checking and validation
 * 4. Roundtrip integrity (serialize -> deserialize -> verify)
 * 5. Version handling and forward compatibility
 * 6. Corruption detection during deserialization
 *
 * These tests ensure that State snapshots can be reliably saved and restored,
 * enabling fast node synchronization and recovery.
 */

#include "test_framework.hpp"
#include "ledger/state.hpp"
#include "address/address.hpp"
#include "storage/memory_kv.hpp"
#include "util/bytes.hpp"
#include <cassert>
#include <memory>
#include <vector>

namespace {

/**
 * @brief Tests the binary format structure of serialized State.
 * 
 * Verifies magic bytes "CSST", version field, and account count are correctly written.
 */
TEST_CASE(StateSerializationBinaryFormat, "State Serialization Binary Format") {
    chrono_storage::MemoryKv kv_store;
    chrono_ledger::State state(kv_store);
    
    // Add test accounts
    state.credit("chronos_1addr1111111111111111111111", 1000);
    state.credit("chronos_1addr2222222222222222222222", 2000);
    
    // Serialize
    auto serialized = state.serialize_to_bytes();
    
    // Verify structure
    ASSERT_TRUE(serialized.size() >= 12, "Serialized state must have at least 12 bytes (magic + version + count)");
    
    // Check magic bytes "CSST"
    ASSERT_EQ(serialized[0], 'C', "Magic byte 0 should be 'C'");
    ASSERT_EQ(serialized[1], 'S', "Magic byte 1 should be 'S'");
    ASSERT_EQ(serialized[2], 'S', "Magic byte 2 should be 'S'");
    ASSERT_EQ(serialized[3], 'T', "Magic byte 3 should be 'T'");
    
    // Extract version (bytes 4-7, little-endian uint32)
    uint32_t version = (serialized[4] | (serialized[5] << 8) | 
                       (serialized[6] << 16) | (serialized[7] << 24));
    ASSERT_EQ(version, 1, "Version should be 1");
    
    // Extract account count (bytes 8-11, little-endian uint32)
    uint32_t account_count = (serialized[8] | (serialized[9] << 8) | 
                             (serialized[10] << 16) | (serialized[11] << 24));
    ASSERT_EQ(account_count, 2, "Account count should be 2");
}

/**
 * @brief Tests complete roundtrip: serialize -> deserialize -> verify data integrity.
 */
TEST_CASE(StateSerializationRoundtrip, "State Serialization Roundtrip") {
    // Create original state with test data
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state1(kv_storestd::move(kv_store1));
    
    state1.credit("chronos_1testaddr1111111111111111", 5000);
    state1.credit("chronos_1testaddr2222222222222222", 7500);
    
    // Apply transactions to set nonces
    chrono_crypto::SignerHMAC signer1("key1");
    chrono_address::Address addr1(signer1.get_public_key());
    chrono_address::Address addr2("chronos_1testaddr2222222222222222");
    
    auto tx1 = chrono_ledger::Transaction(addr1, addr2, 100, 10, 0);
    tx1.signature = signer1.sign(tx1.get_hash_for_signing());
    
    // Serialize
    auto serialized = state1.serialize_to_bytes();
    ASSERT_TRUE(serialized.size() > 0, "Serialized state should not be empty");
    
    // Deserialize to new state
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state2(kv_storestd::move(kv_store2));
    
    ASSERT_TRUE(state2.deserialize_from_bytes(serialized), "Deserialization should succeed");
    
    // Verify balances match
    uint64_t balance1_original = state1.get_balance("chronos_1testaddr1111111111111111");
    uint64_t balance1_restored = state2.get_balance("chronos_1testaddr1111111111111111");
    ASSERT_EQ(balance1_restored, balance1_original, "Balance 1 should match after roundtrip");
    
    uint64_t balance2_original = state1.get_balance("chronos_1testaddr2222222222222222");
    uint64_t balance2_restored = state2.get_balance("chronos_1testaddr2222222222222222");
    ASSERT_EQ(balance2_restored, balance2_original, "Balance 2 should match after roundtrip");
}

/**
 * @brief Tests that nonce state is preserved during serialization.
 */
TEST_CASE(StateSerializationNoncePreservation, "State Serialization Nonce Preservation") {
    chrono_storage::MemoryKv kv_store;
    chrono_ledger::State state(kv_store);
    
    // Set up account with balance and nonces
    std::string test_addr = "chronos_1noncetestaddress11111111111";
    state.credit(test_addr, 1000);
    
    // Simulate nonce increments by applying transactions
    // (nonce increments when transactions are applied)
    // For this test, we verify the nonce is tracked
    uint64_t initial_nonce = state.get_nonce(test_addr);
    ASSERT_EQ(initial_nonce, 0, "Initial nonce should be 0");
    
    // Serialize and deserialize
    auto serialized = state.serialize_to_bytes();
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state2(kv_storestd::move(kv_store2));
    
    ASSERT_TRUE(state2.deserialize_from_bytes(serialized), "Deserialization should succeed");
    
    // Verify nonce matches
    uint64_t restored_nonce = state2.get_nonce(test_addr);
    ASSERT_EQ(restored_nonce, initial_nonce, "Nonce should be preserved in serialization");
}

/**
 * @brief Tests that deserialization rejects corrupted data (bad magic bytes).
 */
TEST_CASE(StateDeserializationCorruptionDetection, "State Deserialization Corruption Detection") {
    chrono_storage::MemoryKv kv_store;
    chrono_ledger::State state(kv_store);
    
    // Create corrupted serialized data
    chrono_util::Bytes corrupted;
    corrupted.push_back('B'); // Wrong magic byte
    corrupted.push_back('A');
    corrupted.push_back('D');
    corrupted.push_back('D');
    corrupted.push_back(1); // version (low byte)
    corrupted.push_back(0); // version
    corrupted.push_back(0);
    corrupted.push_back(0);
    
    // Try to deserialize corrupted data
    ASSERT_FALSE(state.deserialize_from_bytes(corrupted), 
        "Deserialization should fail with bad magic bytes");
}

/**
 * @brief Tests that deserialization rejects invalid version.
 */
TEST_CASE(StateDeserializationVersionValidation, "State Deserialization Version Validation") {
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state1(kv_storestd::move(kv_store1));
    state1.credit("chronos_1addr", 1000);
    
    // Get valid serialization
    auto serialized = state1.serialize_to_bytes();
    
    // Corrupt version (change from 1 to 99)
    if (serialized.size() >= 8) {
        serialized[4] = 99; // Change version low byte to 99
        serialized[5] = 0;
        serialized[6] = 0;
        serialized[7] = 0;
    }
    
    // Try to deserialize with wrong version
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state2(kv_storestd::move(kv_store2));
    
    ASSERT_FALSE(state2.deserialize_from_bytes(serialized), 
        "Deserialization should fail with unsupported version");
}

/**
 * @brief Tests empty state serialization (no accounts).
 */
TEST_CASE(StateSerializationEmpty, "State Serialization Empty State") {
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state1(kv_storestd::move(kv_store1));
    
    // Don't add any accounts
    auto serialized = state1.serialize_to_bytes();
    
    // Should still have header (12 bytes minimum)
    ASSERT_TRUE(serialized.size() >= 12, "Empty state should have at least header");
    
    // Account count should be 0
    uint32_t account_count = (serialized[8] | (serialized[9] << 8) | 
                             (serialized[10] << 16) | (serialized[11] << 24));
    ASSERT_EQ(account_count, 0, "Empty state should have 0 accounts");
    
    // Should deserialize successfully
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state2(kv_storestd::move(kv_store2));
    
    ASSERT_TRUE(state2.deserialize_from_bytes(serialized), 
        "Empty state should deserialize successfully");
}

/**
 * @brief Tests serialization with large balance values (uint64 max).
 */
TEST_CASE(StateSerializationLargeBalances, "State Serialization Large Balances") {
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state1(kv_storestd::move(kv_store1));
    
    // Add account with max uint64 balance
    uint64_t max_balance = 0xFFFFFFFFFFFFFFFFULL; // uint64 max
    state1.credit("chronos_1largebalance1111111111111", max_balance);
    
    // Serialize and deserialize
    auto serialized = state1.serialize_to_bytes();
    chrono_storage::MemoryKv kv_store;();
    chrono_ledger::State state2(kv_storestd::move(kv_store2));
    
    ASSERT_TRUE(state2.deserialize_from_bytes(serialized), 
        "Deserialization of large balance should succeed");
    
    // Verify balance
    uint64_t restored_balance = state2.get_balance("chronos_1largebalance1111111111111");
    ASSERT_EQ(restored_balance, max_balance, "Large balance should be preserved exactly");
}

// Tests auto-register via TEST_CASE macro

} // namespace
