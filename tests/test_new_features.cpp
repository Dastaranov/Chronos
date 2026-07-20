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
#include "ledger/block.hpp"
#include "ledger/transaction.hpp"
#include "address/address.hpp"
#include "crypto/signer_hmac.hpp"
#include "consensus/beacon_engine.hpp"
#include "hardware/time_oracle.hpp"
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
 * @brief Tests that transaction security tier defaults and round-trips through serialization.
 */
void test_transaction_security_tier_roundtrip() {
    chrono_crypto::SignerHMAC signer("tx-tier-key");
    chrono_address::Address from(signer.get_public_key());
    chrono_address::Address to(signer.get_public_key());

    chrono_ledger::Transaction default_tx(from, to, 1, 1, 0, signer.get_public_key());
    assert(default_tx.tier == chrono_ledger::SecurityTier::STANDARD_RETAIL);

    chrono_ledger::Transaction tier1_tx(
        from, to, 2, 1, 1, signer.get_public_key(),
        chrono_ledger::TransactionType::TRANSFER, {},
        chrono_ledger::SecurityTier::CRITICAL_SETTLEMENT
    );
    tier1_tx.signature = signer.sign(tier1_tx.get_hash_for_signing());

    chrono_ledger::Transaction deserialized = chrono_ledger::Transaction::deserialize(tier1_tx.serialize());
    assert(deserialized.tier == chrono_ledger::SecurityTier::CRITICAL_SETTLEMENT);
}

/**
 * @brief Tests that layer_1_anchor is preserved in block serialization.
 */
void test_block_layer1_anchor_roundtrip() {
    chrono_util::Bytes prev_hash(32, 1);
    chrono_util::Bytes anchor(32, 9);
    chrono_ledger::Block block(prev_hash, 1, 1000, 0, 5, 0, {}, anchor);

    chrono_ledger::Block decoded = chrono_ledger::Block::deserialize(block.serialize());
    assert(decoded.layer_1_anchor == anchor);
}

namespace {
class FixedOracle : public chrono_hardware::ITimeOracle {
public:
    explicit FixedOracle(uint64_t fixed_ts) : ts_(fixed_ts) {}
    uint64_t get_hardware_timestamp() override { return ts_; }
private:
    uint64_t ts_;
};
} // namespace

/**
 * @brief Tests that BeaconEngine uses the injected time oracle.
 */
void test_beacon_engine_uses_injected_time_oracle() {
    chrono_crypto::SignerHMAC signer("beacon-engine-key");
    auto oracle = std::make_unique<FixedOracle>(42424242);
    chrono_consensus::BeaconEngine engine(&signer, std::move(oracle), 1);

    auto beat = engine.maybe_produce(std::chrono::steady_clock::now() + std::chrono::milliseconds(2));
    assert(beat.has_value());
    assert(beat->timestamp_ms == 42424242);
}

/**
 * @struct Registrar
 * @brief Auto-registers test cases.
 */
struct Registrar {
    Registrar() {
        test_framework::register_test("Nonce Tracking", test_nonce_tracking);
        test_framework::register_test("State Serialization", test_state_serialization);
        test_framework::register_test("Transaction Security Tier Roundtrip", test_transaction_security_tier_roundtrip);
        test_framework::register_test("Block Layer1 Anchor Roundtrip", test_block_layer1_anchor_roundtrip);
        test_framework::register_test("Beacon Engine Uses Injected Oracle", test_beacon_engine_uses_injected_time_oracle);
    }
};

static Registrar registrar;

} // namespace
