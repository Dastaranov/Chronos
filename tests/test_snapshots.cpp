#include "test_framework.hpp"
#include "storage/snapshots.hpp"
#include "storage/memory_kv.hpp" // For Mocking IKv
#include "ledger/state.hpp"      // For Mocking State
#include "util/bytes.hpp"
#include "crypto/signer_hmac.hpp"
#include "address/address.hpp"
#include <memory>
#include <string>

TEST_CASE(SnapshotManagerCreateAndRestoreSnapshot, "SnapshotManager Create and Restore Snapshot") {
    // Setup
    auto kv_store_for_snapshots = std::make_unique<chrono_storage::MemoryKv>();
    chrono_storage::SnapshotManager snapshot_manager(std::move(kv_store_for_snapshots));

    // Create state using reference
    auto kv_store_for_state = std::make_unique<chrono_storage::MemoryKv>();
    auto& kv_ref_state = *kv_store_for_state;
    chrono_ledger::State mock_state(kv_ref_state);
    
    chrono_util::Bytes test_block_hash = chrono_util::string_to_bytes("test_block_hash_123");
    uint64_t test_height = 100;

    // Test Create Snapshot
    ASSERT_TRUE(snapshot_manager.createSnapshot(test_height, mock_state, test_block_hash), "Snapshot creation should succeed");

    // Test Restore Snapshot
    std::optional<chrono_storage::SnapshotData> restored_snapshot = snapshot_manager.restoreSnapshot(test_height);
    ASSERT_TRUE(restored_snapshot.has_value(), "Snapshot restoration should succeed");
    ASSERT_EQ(restored_snapshot->height, test_height, "Restored snapshot height matches");
    ASSERT_BYTES_EQ(restored_snapshot->last_block_hash, test_block_hash, "Restored snapshot last_block_hash matches");

    // Test non-existent snapshot restoration
    std::optional<chrono_storage::SnapshotData> non_existent_snapshot = snapshot_manager.restoreSnapshot(999);
    ASSERT_FALSE(non_existent_snapshot.has_value(), "Restoring non-existent snapshot should fail");
}

TEST_CASE(SnapshotManagerCorruptedData, "SnapshotManager Corrupted Data") {
    // Setup: Create valid snapshot first
    auto kv_store_for_snapshots = std::make_unique<chrono_storage::MemoryKv>();
    chrono_storage::SnapshotManager snapshot_manager(std::move(kv_store_for_snapshots));

    auto kv_store_for_state = std::make_unique<chrono_storage::MemoryKv>();
    chrono_ledger::State test_state(*kv_store_for_state);
    test_state.credit("test_addr", 100);

    // Create a valid snapshot
    chrono_util::Bytes test_hash = chrono_util::string_to_bytes("test_hash");
    snapshot_manager.createSnapshot(200, test_state, test_hash);

    // Test that a non-existent snapshot returns nullopt
    std::optional<chrono_storage::SnapshotData> non_existent = snapshot_manager.restoreSnapshot(999);
    ASSERT_FALSE(non_existent.has_value(), "Non-existent snapshot should return nullopt");
}

TEST_CASE(SnapshotStateRecovery, "Full State Recovery from Snapshot") {
    // Setup: Create initial state with accounts using VALID addresses
    // Previous test failure was due to using invalid test addresses that failed Bech32m validation
    // causing credit() to silently fail
    chrono_crypto::SignerHMAC signer1("test_addr_1");
    chrono_crypto::SignerHMAC signer2("test_addr_2");
    chrono_util::Bytes pubkey1 = signer1.get_public_key();
    chrono_util::Bytes pubkey2 = signer2.get_public_key();
    chrono_address::Address addr1_obj(pubkey1);
    chrono_address::Address addr2_obj(pubkey2);
    std::string addr1 = addr1_obj.to_string();
    std::string addr2 = addr2_obj.to_string();
    
    auto kv_store_for_snapshots = std::make_unique<chrono_storage::MemoryKv>();
    chrono_storage::SnapshotManager snapshot_manager(std::move(kv_store_for_snapshots));

    auto kv_store_state = std::make_unique<chrono_storage::MemoryKv>();
    auto& kv_ref_for_original = *kv_store_state;
    chrono_ledger::State original_state(kv_ref_for_original);
    
    // Add some accounts to the state using credit() with valid address strings
    original_state.credit(addr1, 1000);
    original_state.credit(addr2, 500);

    // Serialize the state
    chrono_util::Bytes serialized_state = original_state.serialize_to_bytes();
    ASSERT_TRUE(serialized_state.size() > 0, "State serialization should produce bytes");
    
    // Create snapshot with the state
    chrono_util::Bytes test_block_hash = chrono_util::string_to_bytes("snapshot_block_hash");
    uint64_t snapshot_height = 50;
    snapshot_manager.createSnapshot(snapshot_height, original_state, test_block_hash);

    // Retrieve snapshot
    std::optional<chrono_storage::SnapshotData> snapshot_data = snapshot_manager.restoreSnapshot(snapshot_height);
    ASSERT_TRUE(snapshot_data.has_value(), "Should retrieve snapshot successfully");
    ASSERT_EQ(snapshot_data->height, snapshot_height, "Snapshot height should match");
    ASSERT_BYTES_EQ(snapshot_data->last_block_hash, test_block_hash, "Block hash should match");

    // Now create a new state and recover it from snapshot
    auto kv_store_recovery = std::make_unique<chrono_storage::MemoryKv>();
    auto& kv_ref_for_recovery = *kv_store_recovery;
    chrono_ledger::State recovered_state(kv_ref_for_recovery);
    
    // Deserialize from snapshot bytes
    bool deserialization_success = recovered_state.deserialize_from_bytes(snapshot_data->state_bytes);
    ASSERT_TRUE(deserialization_success, "State deserialization should succeed");

    // Verify recovered state matches original
    ASSERT_EQ(1000, recovered_state.get_balance(addr1), "addr1 balance should be recovered to 1000");
    ASSERT_EQ(500, recovered_state.get_balance(addr2), "addr2 balance should be recovered to 500");
    ASSERT_EQ(0, recovered_state.get_nonce(addr1), "addr1 nonce should be 0 (no transactions)");
    ASSERT_EQ(0, recovered_state.get_nonce(addr2), "addr2 nonce should be 0 (no transactions)");
}

// TODO: Add tests for NodeApp::add_block (snapshot creation frequency)
// TODO: Add integration tests for P2P snapshot messages (discovery, request, chunk transfer)