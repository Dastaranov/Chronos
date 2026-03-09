#include "test_framework.hpp"
#include "ledger/state.hpp"
#include "ledger/transaction.hpp"
#include "storage/memory_kv.hpp"
#include "address/address.hpp"
#include "util/bytes.hpp"

using namespace chrono_ledger;
using namespace chrono_storage;
using namespace chrono_address;
using namespace chrono_util;

TEST_CASE(KeyRotationTransaction, "Key Rotation Transaction") {
    MemoryKv kv;
    State state(kv);

    // Setup sender
    std::string sender_addr = "cqc19v2x7p9ckvfvwfz8wpm00c6c85sf655l8w5640"; // Valid generated address
    state.credit(sender_addr, 1000000); // Fund the account
    // Genesis has 1000000 balance
    
    // Create rotation transaction
    Transaction tx;
    tx.type = TransactionType::KEY_ROTATION;
    tx.sender = Address(sender_addr);
    tx.recipient = Address(sender_addr); // Irrelevant
    tx.amount = 0;
    tx.fee = 100;
    tx.nonce = 1; // Genesis nonce is 0
    
    // New key payload (dummy)
    Bytes new_key = {0x01, 0x02, 0x03, 0x04};
    tx.payload = new_key;
    
    // Apply
    bool success = state.apply_transaction(tx);
    ASSERT_TRUE(success, "Transaction should succeed");
    
    // Verify balance deducted
    ASSERT_EQ(state.get_balance(sender_addr), 1000000 - 100, "Balance should be deducted");
    
    // Verify key updated
    Bytes stored_key = state.get_validator_key(sender_addr);
    ASSERT_EQ(stored_key, new_key, "Key should be updated");
    
    // Verify nonce incremented
    ASSERT_EQ(state.get_nonce(sender_addr), 1, "Nonce should be incremented");
}

TEST_CASE(KeyRotationValidation, "Key Rotation Validation") {
    MemoryKv kv;
    State state(kv);
    std::string sender_addr = "cqc19v2x7p9ckvfvwfz8wpm00c6c85sf655l8w5640";
    state.credit(sender_addr, 1000000); // Fund the account

    // Fail if amount > 0
    Transaction tx1;
    tx1.type = TransactionType::KEY_ROTATION;
    tx1.sender = Address(sender_addr);
    tx1.amount = 10;
    tx1.fee = 100;
    tx1.nonce = 1;
    tx1.payload = {0x01};
    
    ASSERT_FALSE(state.apply_transaction(tx1), "Should fail if amount > 0");
    
    // Fail if payload empty
    Transaction tx2;
    tx2.type = TransactionType::KEY_ROTATION;
    tx2.sender = Address(sender_addr);
    tx2.amount = 0;
    tx2.fee = 100;
    tx2.nonce = 1;
    // payload empty
    
    ASSERT_FALSE(state.apply_transaction(tx2), "Should fail if payload empty");
}
