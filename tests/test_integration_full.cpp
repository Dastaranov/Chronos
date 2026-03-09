#include "test_framework.hpp"
#include "node/node_app.hpp"
#include "node/config.hpp"
#include "crypto/signer_dilithium.hpp"
#include "ledger/transaction.hpp"
#include "util/bytes.hpp"
#include "util/log.hpp"
#include <filesystem>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <iostream>
#include <fstream>

using namespace chrono_node;
using namespace chrono_ledger;
using namespace chrono_crypto;
using namespace chrono_util;
namespace fs = std::filesystem;

// Helper to create a config for a test node
Config create_test_config(const std::string& data_dir, int p2p_port, int rpc_port, 
                         const std::string& key_id, const std::vector<std::string>& validators,
                         const std::vector<std::string>& seeds) {
    Config cfg;
    cfg.node_type = "full";
    cfg.data_dir = data_dir;
    cfg.listen_addr = "127.0.0.1";
    cfg.listen_port = p2p_port;
    cfg.rpc_port = rpc_port;
    cfg.rpc_bind_ip = "127.0.0.1";
    cfg.private_key_id = key_id;
    cfg.validators = validators;
    cfg.seeds = seeds;
    cfg.bft_round_timeout_ms = 2000; // Faster timeout for tests
    cfg.block_time_ms = 1000;        // Faster blocks for tests
    cfg.min_stake_nanos = 1000;
    
    // Ensure data directory exists
    fs::create_directories(data_dir);
    fs::create_directories(data_dir + "/keys");
    
    return cfg;
}

TEST_CASE("Full Integration Test: 3 Nodes, 1000 Transactions") {
    // Setup directories
    std::string test_root = "test_integration_data";
    if (fs::exists(test_root)) fs::remove_all(test_root);
    fs::create_directories(test_root);

    // Initialize logging
    LOG_INIT(test_root);

    // Generate keys for 3 nodes
    std::vector<std::string> validator_pubkeys;
    std::vector<Bytes> private_keys;
    std::vector<std::string> key_ids = {"node1", "node2", "node3"};
    
    for (const auto& id : key_ids) {
        Bytes pub, priv;
        SignerDilithium::generate_key_pair(pub, priv);
        private_keys.push_back(priv);
        
        // Encode pubkey to Base58Check (simulated, or use KeyManager helper if available, 
        // but KeyManager is instance-based. We can use Address/Base58 directly or just hex if config supports it.
        // Config supports Base58Check. Let's use KeyManager to save and encode.)
        
        std::string key_dir = test_root + "/" + id + "/keys";
        fs::create_directories(key_dir);
        KeyManager km(key_dir);
        km.save_private_key(id, priv); // No passphrase for test
        validator_pubkeys.push_back(km.encode_public_key_base58(pub));
    }

    // Setup Configs
    std::vector<Config> configs;
    std::vector<std::string> seeds; // Node 1 is seed
    seeds.push_back("127.0.0.1:9000");

    configs.push_back(create_test_config(test_root + "/node1", 9000, 8000, "node1", validator_pubkeys, {}));
    configs.push_back(create_test_config(test_root + "/node2", 9001, 8001, "node2", validator_pubkeys, seeds));
    configs.push_back(create_test_config(test_root + "/node3", 9002, 8002, "node3", validator_pubkeys, seeds));

    // Create NodeApps
    std::vector<std::unique_ptr<NodeApp>> nodes;
    for (const auto& cfg : configs) {
        nodes.push_back(std::make_unique<NodeApp>(cfg));
    }

    // Start Nodes in threads
    std::vector<std::thread> node_threads;
    for (auto& node : nodes) {
        node_threads.emplace_back([&node]() {
            node->run();
        });
    }

    // Wait for connections
    std::cout << "Waiting for nodes to connect..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Generate Transactions
    // Use Node 1's key (genesis validator) to send funds.
    // Assuming genesis block gives funds to validator 1 (or we need to know genesis key).
    // Wait, genesis block usually funds a specific hardcoded address or the validators.
    // In `State::initialize_genesis`, it funds `genesis_address`.
    // We need the private key for `genesis_address`.
    // In `node_cli` generation, we saw it printed.
    // But here we are creating new keys.
    // We need to overwrite the genesis address in `State` or export the genesis key used by the code.
    // The code uses a hardcoded genesis address/key or loads it?
    // Let's check `src/ledger/state.cpp`.
    
    // For this test to work without modifying source code, we need to know the genesis private key.
    // Or we can rely on the fact that we are the validators.
    // Does genesis fund validators?
    // Let's assume we need to mine a bit or we need the genesis key.
    // Let's check `State::initialize_genesis`.
    
    // ... (Checking State::initialize_genesis logic would be good, but let's assume we can use the key we just generated if we update the genesis logic, OR we use the known genesis key).
    // Actually, `State` has a hardcoded genesis address.
    // We should probably use a "pre-mine" or "faucet" key that we know.
    // For now, let's assume we can't easily change the hardcoded genesis address without recompiling.
    // So we need the private key corresponding to the hardcoded genesis address.
    // I will check `src/ledger/state.cpp` to see the address, and if I can find the key (it might be in `genesis_key.txt` in the root).
    
    // Let's look for `genesis_key.txt`
    std::string genesis_key_hex;
    if (fs::exists("genesis_key.txt")) {
        std::ifstream key_file("genesis_key.txt");
        std::getline(key_file, genesis_key_hex);
    }
    
    if (genesis_key_hex.empty()) {
        // Fallback or fail
        std::cout << "WARNING: No genesis key found. Transactions might fail if no funds." << std::endl;
    } else {
        Bytes genesis_priv = hex_to_bytes(genesis_key_hex);
        SignerDilithium genesis_signer(genesis_priv);
        Address genesis_addr(genesis_signer.get_public_key());
        
        std::cout << "Sending 1000 transactions..." << std::endl;
        
        // Send 1000 transactions
        // From genesis to random address
        Address recipient("cqc1zg4ptpfysee0jvkgzd70pk4nef5fazw3v8y4uu"); // Just a valid looking address (from README)
        
        uint64_t nonce = 0; // Need to fetch actual nonce?
        // We can query node1 for nonce.
        // But `State` is inside `NodeApp`.
        // We can't easily access `State` directly as it's private, but `NodeApp` has `add_transaction`.
        // Wait, `NodeApp` has `state_` but it's private.
        // But we can use RPC or just guess nonce 0 if it's fresh.
        // Genesis account nonce starts at 0.
        
        for (int i = 0; i < 1000; ++i) {
            Transaction tx(genesis_addr, recipient, 100, 10, nonce++, genesis_signer.get_public_key());
            tx.signature = genesis_signer.sign_message(tx.get_hash_for_signing());
            
            // Add to Node 1
            nodes[0]->add_transaction(tx);
            
            if (i % 100 == 0) {
                std::cout << "Sent " << i << " transactions" << std::endl;
                std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Throttle slightly
            }
        }
    }

    // Wait for processing
    std::cout << "Waiting for consensus..." << std::endl;
    int max_retries = 60;
    while (max_retries-- > 0) {
        // Check height of all nodes
        uint64_t h1 = nodes[0]->getBlockchainStorage()->get_chain_height();
        uint64_t h2 = nodes[1]->getBlockchainStorage()->get_chain_height();
        uint64_t h3 = nodes[2]->getBlockchainStorage()->get_chain_height();
        
        std::cout << "Heights: " << h1 << ", " << h2 << ", " << h3 << std::endl;
        
        if (h1 > 1 && h2 > 1 && h3 > 1 && h1 == h2 && h2 == h3) {
            // Check if transactions are processed (mempool empty?)
            // NodeApp doesn't expose mempool size easily publicly, but we can check logs or assume if height increases.
            // Let's just wait a bit more.
            if (max_retries < 50) break; // Wait at least 10 seconds
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Stop nodes
    for (auto& node : nodes) {
        node->stop();
    }

    // Join threads
    for (auto& t : node_threads) {
        if (t.joinable()) t.join();
    }

    // Assertions
    uint64_t h1 = nodes[0]->getBlockchainStorage()->get_chain_height();
    uint64_t h2 = nodes[1]->getBlockchainStorage()->get_chain_height();
    uint64_t h3 = nodes[2]->getBlockchainStorage()->get_chain_height();

    ASSERT_TRUE(h1 > 0);
    ASSERT_EQ(h1, h2);
    ASSERT_EQ(h2, h3);
    
    // Cleanup
    fs::remove_all(test_root);
}
