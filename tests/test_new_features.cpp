//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_new_features.cpp
 * @brief Simple tests for newly implemented production-readiness features.
 *
 * Tests:
 * 1. State::get_nonce() - Nonce tracking for replay attack prevention
 * 2. State::serialize_to_bytes() / deserialize_from_bytes() - State persistence
 */

#include "test_framework.hpp"
#include "ledger/state.hpp"
#include "ledger/transaction.hpp"
#include "address/address.hpp"
#include "crypto/signer_hmac.hpp"
#include "storage/memory_kv.hpp"
#include "util/bytes.hpp"

namespace {

/**
 * @brief Tests that nonces are tracked and incremented per address.
 */
void test_nonce_tracking() {
    chrono_storage::MemoryKv kv;
    chrono_ledger::State state(kv);
    
    // Create signer and addresses
    chrono_crypto::SignerHMAC signer("key");
    chrono_address::Address from_addr(signer.get_public_key());
    chrono_address::Address to_addr(signer.get_public_key()); // Use valid address
    
    // Initial nonce should be 0
    uint64_t nonce = state.get_nonce(from_addr.to_string());
    assert(nonce == 0);
    
    // Credit from address
    state.credit(from_addr.to_string(), 500);
    
    // Create and apply transaction
    chrono_ledger::Transaction tx(from_addr, to_addr, 100, 10, 0, signer.get_public_key());
    tx.signature = signer.sign(tx.get_hash_for_signing());
    
    state.apply_transaction(tx);
    
    // Nonce should be incremented to 1
    uint64_t new_nonce = state.get_nonce(from_addr.to_string());
    assert(new_nonce == 1);
}

/**
 * @brief Tests state serialization and deserialization roundtrip.
 */
void test_state_serialization() {
    // Create valid addresses using SignerHMAC
    auto signer1 = std::make_unique<chrono_crypto::SignerHMAC>("test_state_addr1");
    auto signer2 = std::make_unique<chrono_crypto::SignerHMAC>("test_state_addr2");
    chrono_address::Address addr1(signer1->get_public_key());
    chrono_address::Address addr2(signer2->get_public_key());
    std::string addr1_str = addr1.to_string();
    std::string addr2_str = addr2.to_string();
    
    chrono_storage::MemoryKv kv1;
    chrono_ledger::State state1(kv1);
    
    // Add some balances with valid addresses
    state1.credit(addr1_str, 1000);
    state1.credit(addr2_str, 2000);
    
    // Serialize
    auto serialized = state1.serialize_to_bytes();
    assert(serialized.size() > 0);
    
    // Check magic bytes
    assert(serialized[0] == 'C');
    assert(serialized[1] == 'S');
    assert(serialized[2] == 'S');
    assert(serialized[3] == 'T');
    
    // Deserialize to new state
    chrono_storage::MemoryKv kv2;
    chrono_ledger::State state2(kv2);
    
    bool success = state2.deserialize_from_bytes(serialized);
    assert(success);
    
    // Verify balances match (using same address strings from above)
    uint64_t balance1 = state2.get_balance(addr1_str);
    uint64_t balance2 = state2.get_balance(addr2_str);
    
    assert(balance1 == 1000);
    assert(balance2 == 2000);
}

/**
 * @struct Registrar
 * @brief Auto-registers test cases.
 */
struct Registrar {
    Registrar() {
        test_framework::register_test("Nonce Tracking", test_nonce_tracking);
        test_framework::register_test("State Serialization", test_state_serialization);
    }
};

static Registrar registrar;

} // namespace
