//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_ledger.cpp
 * @brief This file contains unit tests for the core ledger functionalities of the Chronos project.
 *
 * It focuses on testing the `State`, `Block`, and `Transaction` classes,
 * including transaction application, balance updates, and block processing.
 * The tests utilize a simple HMAC signer for transaction signing and the
 * custom test framework for assertion handling.
 *
 * Key tests include:
 * - `test_ledger_logic()`: Simulates a basic transaction flow, including
 *   address creation, initial crediting, transaction signing, block creation,
 *   and state application, verifying final balances.
 */

#include "ledger/state.hpp"
#include "ledger/block.hpp"
#include "ledger/transaction.hpp"
#include "address/address.hpp"
#include "crypto/signer_hmac.hpp"
#include "test_framework.hpp"
#include "util/log.hpp" // For logging within tests
#include <cassert>
#include <vector>
#include <string>
#include <chrono>

#include "storage/memory_kv.hpp"

namespace {

/**
 * @brief Tests the fundamental logic of the Chronos ledger, including state, transactions, and blocks.
 *
 * This test case simulates a simple blockchain scenario:
 * 1. Two HMAC signers are created to represent different users.
 * 2. Addresses are derived from these signers' public keys.
 * 3. An initial balance is credited to the first address in the `State`.
 * 4. A transaction is created from the first address to the second, including an amount and a fee.
 * 5. The transaction is signed by the first signer.
 * 6. A block is created and the signed transaction is added to it.
 * 7. The block's transactions are applied to the `State`.
 * 8. Finally, assertions verify that the balances of both addresses are correct after the transaction.
 */
void test_ledger_logic() {
    // Create signers for testing purposes
    chrono_crypto::SignerHMAC signer1("test_key1"); ///< @var signer1 First test signer with a unique key.
    chrono_crypto::SignerHMAC signer2("test_key2"); ///< @var signer2 Second test signer with a unique key.

    // Create addresses from the signers' public keys
    chrono_address::Address addr1(signer1.get_public_key()); ///< @var addr1 Address corresponding to signer1.
    chrono_address::Address addr2(signer2.get_public_key()); ///< @var addr2 Address corresponding to signer2.

    // Create a state and credit the first address with an initial amount
    chrono_storage::MemoryKv kv_store;
    chrono_ledger::State state(kv_store); ///< @var state The ledger state object for testing.
    state.credit(addr1.to_string(), 1000); // Give addr1 1000 units

    // Create a transaction from addr1 to addr2
    chrono_ledger::Transaction tx(addr1, addr2, 100, 10, 0, signer1.get_public_key()); ///< @var tx A new transaction: addr1 sends 100 to addr2 with a 10 unit fee.
    // Sign the transaction using signer1's private key
    tx.signature = signer1.sign(tx.get_hash_for_signing());

    // Create a block and add the transaction to it
    chrono_ledger::Block block; ///< @var block A new block object.
    block.height = 1; // Set block height
    block.transactions.push_back(tx); // Add the signed transaction to the block

    // Apply the block's transactions to the state
    for (const auto& t : block.transactions) {
        ASSERT_TRUE(state.apply_transaction(t), "Transaction application should succeed");
    }

    // STANDARD transfers are queued and settled later, so balances are unchanged immediately.
    uint64_t addr1_balance = state.get_balance(addr1.to_string());
    uint64_t addr2_balance = state.get_balance(addr2.to_string());
    LOG_INFO(chrono_util::LogCategory::GENERAL, "addr1 balance: {}", addr1_balance);
    LOG_INFO(chrono_util::LogCategory::GENERAL, "addr2 balance: {}", addr2_balance);
    ASSERT_EQ(1000, addr1_balance, "addr1 balance should remain 1000 before settlement");
    ASSERT_EQ(0, addr2_balance, "addr2 balance should remain 0 before settlement");

    // Advance settlement time beyond the pending delay and apply queued transfer.
    const uint64_t settle_time = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    ) + 61;
    state.process_pending_settlements(settle_time);

    // Check balances after settlement.
    addr1_balance = state.get_balance(addr1.to_string());
    addr2_balance = state.get_balance(addr2.to_string());
    ASSERT_EQ(890, addr1_balance, "addr1 balance should be 890 (1000 - 100 - 10)");
    ASSERT_EQ(100, addr2_balance, "addr2 balance should be 100");
}

/**
 * @struct Registrar
 * @brief A helper struct to automatically register test cases with the test framework.
 *
 * This struct's constructor is executed at static initialization time,
 * registering `test_ledger_logic` with the `test_framework`.
 */
struct Registrar {
    Registrar() {
        test_framework::register_test("Ledger Logic", test_ledger_logic);
    }
};

static Registrar registrar; ///< @var registrar Static instance of Registrar to trigger test registration.

} // namespace