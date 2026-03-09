//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_merkle_root.cpp
 * @brief Unit tests for Merkle root calculation and validation.
 *
 * Tests domain separation in merkle tree computation, empty block handling,
 * merkle root verification during deserialization, and block validation.
 */

#include "ledger/block.hpp"
#include "ledger/transaction.hpp"
#include "address/address.hpp"
#include "crypto/signer_hmac.hpp"
#include "test_framework.hpp"
#include "util/log.hpp"
#include <vector>

namespace {

// Helper function to create a proper 32-byte hash
chrono_util::Bytes make_32byte_hash(const std::string& label) {
    std::string padded = label;
    while (padded.size() < 32) {
        padded += "_";
    }
    if (padded.size() > 32) {
        padded = padded.substr(0, 32);
    }
    return chrono_util::string_to_bytes(padded);
}

/**
 * @brief Test merkle root of empty block
 */
void test_empty_block_merkle_root() {
    chrono_ledger::Block empty_block(
        make_32byte_hash("prev_hash_1"),
        0,
        100,
        0,
        5, // Tier 5
        0, // Score 0
        {}  // Empty transactions
    );

    chrono_util::Bytes merkle1 = empty_block.calculate_merkle_root();
    chrono_util::Bytes merkle2 = empty_block.calculate_merkle_root();
    
    ASSERT_BYTES_EQ(merkle1, merkle2, "Empty block merkle root should be deterministic");
    ASSERT_EQ(merkle1.size(), 32, "Merkle root should be 32 bytes (BLAKE3)");
    
    chrono_util::Bytes empty_hash = chrono_crypto::blake3({});
    ASSERT_FALSE(merkle1 == empty_hash, "Empty block merkle should use domain separation");
}

/**
 * @brief Test merkle root changes when transactions change
 */
void test_merkle_root_changes_with_transactions() {
    chrono_crypto::SignerHMAC signer("test_key");
    chrono_address::Address addr(signer.get_public_key());
    
    chrono_util::Bytes prev_hash = make_32byte_hash("prev_hash_2");
    
    // Block 1: empty
    chrono_ledger::Block block1(prev_hash, 0, 100, 0, 5, 0, {}); // Tier 5, Score 0
    chrono_util::Bytes merkle1 = block1.calculate_merkle_root();
    
    // Block 2: with one transaction
    chrono_ledger::Transaction tx(addr, addr, 100, 10, 0, signer.get_public_key());
    tx.signature = signer.sign(tx.get_hash_for_signing());
    
    std::vector<chrono_ledger::Transaction> txs;
    txs.push_back(tx);
    chrono_ledger::Block block2(prev_hash, 0, 100, 0, 5, 0, txs); // Tier 5, Score 0
    chrono_util::Bytes merkle2 = block2.calculate_merkle_root();
    
    ASSERT_FALSE(merkle1 == merkle2, "Merkle root should change when transactions are added");
}

/**
 * @brief Test merkle root determinism
 */
void test_merkle_root_deterministic() {
    chrono_crypto::SignerHMAC signer("test_key");
    chrono_address::Address addr(signer.get_public_key());
    
    chrono_util::Bytes prev_hash = make_32byte_hash("prev_hash_3");
    
    std::vector<chrono_ledger::Transaction> txs1;
    for (int i = 0; i < 2; i++) {
        chrono_ledger::Transaction tx(addr, addr, 100 + i, 10, i, signer.get_public_key());
        tx.signature = signer.sign(tx.get_hash_for_signing());
        txs1.push_back(tx);
    }
    
    chrono_ledger::Block block1(prev_hash, 0, 100, 0, 5, 0, txs1); // Tier 5, Score 0
    chrono_util::Bytes merkle1 = block1.calculate_merkle_root();
    
    std::vector<chrono_ledger::Transaction> txs2;
    for (int i = 0; i < 2; i++) {
        chrono_ledger::Transaction tx(addr, addr, 100 + i, 10, i, signer.get_public_key());
        tx.signature = signer.sign(tx.get_hash_for_signing());
        txs2.push_back(tx);
    }
    
    chrono_ledger::Block block2(prev_hash, 0, 100, 0, 5, 0, txs2); // Tier 5, Score 0
    chrono_util::Bytes merkle2 = block2.calculate_merkle_root();
    
    ASSERT_BYTES_EQ(merkle1, merkle2, "Same transactions should produce same merkle root");
}

/**
 * @brief Test block size validation
 */
void test_block_is_valid_size_checks() {
    chrono_util::Bytes short_hash = chrono_util::string_to_bytes("short");
    
    chrono_ledger::Block block(short_hash, 0, 100, 0, 5, 0, {}); // Tier 5, Score 0
    ASSERT_FALSE(block.is_valid(), "Block with invalid prev_block_hash size should be invalid");
}

/**
 * @struct MerkleRootRegistrar
 * @brief Helper struct to register merkle root tests
 */
struct MerkleRootRegistrar {
    MerkleRootRegistrar() {
        test_framework::register_test("Empty Block Merkle Root", test_empty_block_merkle_root);
        test_framework::register_test("Merkle Root Changes With Transactions", test_merkle_root_changes_with_transactions);
        test_framework::register_test("Merkle Root Deterministic", test_merkle_root_deterministic);
        test_framework::register_test("Block is_valid Size Checks", test_block_is_valid_size_checks);
    }
};

static MerkleRootRegistrar registrar;

} // namespace
