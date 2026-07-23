#include "node/node_app.hpp"
#include "node/config.hpp"
#include "crypto/key_manager.hpp"
#include "crypto/signer_dilithium.hpp"
#include "util/log.hpp"
#include "util/logging_config.hpp"
#include "util/bytes.hpp"
#include <thread>
#include <filesystem>
#include <vector>
#include <chrono>
#include <atomic>
#include <iostream>
#include <algorithm>

#include "address/address.hpp"
#include "storage/IBlockchainStorage.hpp"
#include "util/codec.hpp"

namespace fs = std::filesystem;
using namespace chrono_node;
using namespace chrono_crypto;
using namespace chrono_util;
using namespace chrono_ledger;
using namespace chrono_address;

// Helper to create a node configuration
Config create_test_config(int id, const std::string& base_dir, const std::vector<std::string>& validators) {
    Config cfg;
    cfg.node_type = NodeType::FULL;
    cfg.data_dir = base_dir + "/node" + std::to_string(id);
    cfg.listen_port = 9000 + id;
    cfg.rpc_port = 8000 + id;
    cfg.bft_round_timeout_ms = 2000; // 2 seconds for test
    cfg.validators = validators;
    cfg.min_stake_nanos = 1000;
    cfg.genesis_consensus_time = 1700000000000; // Set a fixed time to avoid PoT error
    cfg.peer_management_interval_ms = 1000; // 1 second for test
    cfg.peer_discovery_interval_ms = 1000; // 1 second for test
    cfg.enable_peer_discovery = true;
    cfg.network_seeds.clear(); // Clear default seeds
    
    // Create data directory
    fs::create_directories(cfg.data_dir);
    
    return cfg;
}

int main() {
    // Setup logging
    chrono_util::setup_logging(false, false, "test_integration_logs/chronos.log");
    
    std::string test_root = "test_integration_data";
    if (fs::exists(test_root)) {
        fs::remove_all(test_root);
    }
    fs::create_directories(test_root);
    fs::create_directories(test_root + "/keys");

    // 1. Generate Keys for 3 Validators
    std::vector<std::string> validator_pubkeys;
    std::vector<std::string> validator_key_ids;
    std::vector<std::string> validator_addresses;

    KeyManager km(test_root + "/keys");

    for (int i = 0; i < 3; ++i) {
        Bytes pubkey, privkey;
        SignerDilithium::generate_key_pair(pubkey, privkey);
        
        SignerDilithium signer(pubkey, privkey);
        
        // Save key using KeyManager
        std::string key_id = "test-validator-" + std::to_string(i);
        km.save_key_pair(key_id, {pubkey, privkey});
        
        validator_key_ids.push_back(key_id);
        validator_pubkeys.push_back(km.encode_public_key_base58(pubkey));
        validator_addresses.push_back(signer.get_address());
    }

    // 2. Configure Nodes
    std::vector<std::unique_ptr<NodeApp>> nodes;
    std::vector<std::thread> node_threads;
    std::vector<Config> configs;

    // Create a sender account
    Bytes sender_pub, sender_priv;
    SignerDilithium::generate_key_pair(sender_pub, sender_priv);
    SignerDilithium sender_signer(sender_pub, sender_priv); // Use new constructor
    std::string sender_address = sender_signer.get_address();

    for (int i = 0; i < 3; ++i) {
        Config cfg = create_test_config(i, test_root, validator_addresses); // Use addresses for validators list
        // Load keys to set in config (simulating loading from secure storage or passing via env/config)
        auto key_pair = km.load_key_pair(validator_key_ids[i]);
        if (key_pair) {
            cfg.private_key = bytes_to_hex(key_pair->private_key);
            cfg.public_key = bytes_to_hex(key_pair->public_key);
        }
        
        // Genesis allocation to sender
        cfg.genesis_allocations[sender_address] = 1000000000000; // 1 trillion nanos
        
        // Set seeds so they connect to each other
        if (i > 0) {
            cfg.network_seeds.push_back("127.0.0.1:" + std::to_string(9000)); // Connect to node 0
        }
        
        configs.push_back(cfg);
        auto node = std::make_unique<NodeApp>(cfg);
        
        // Pre-populate NodeRegistry with validators so signature verification works
        // This is a hack for the test environment where we don't have on-chain registration yet
        for (size_t j = 0; j < validator_addresses.size(); ++j) {
            node->getState().get_node_registry_mutable().register_node(
                validator_addresses[j], 
                validator_pubkeys[j], 
                "validator-" + std::to_string(j), 
                1000000,
                4 // Time Tier 4
            );
            // Force approval by checking with 0 total validators (threshold 0)
            node->getState().get_node_registry_mutable().check_approval(validator_addresses[j], 0);
        }
        
        nodes.push_back(std::move(node));
    }

    // 3. Start Nodes
    for (int i = 0; i < 3; ++i) {
        node_threads.emplace_back([&nodes, i]() {
            try {
                nodes[i]->run();
            } catch (const std::exception& e) {
                LOG_ERROR(LogCategory::GENERAL, "Node {} failed: {}", i, e.what());
            }
        });
    }

    // Wait for startup and connection
    std::cout << "Waiting for nodes to start and connect..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 4. Generate and Send Transactions
    int tx_count = 1000; // 1000 transactions
    std::cout << "Sending " << tx_count << " transactions..." << std::endl;

    for (int i = 0; i < tx_count; ++i) {
        // Create transaction
        chrono_ledger::Transaction tx(
            Address(sender_address),
            Address(validator_addresses[0]), // Send to validator 0
            100, // Amount
            10, // Fee
            i, // Nonce (start at 0)
            sender_signer.get_public_key() // Public Key
        );
        
        // Sign
        tx.signature = sender_signer.sign_message(tx.get_hash_for_signing());
        
        // Submit to node 0
        nodes[0]->add_transaction(tx);
        
        if (i % 100 == 0) {
            std::cout << "Sent " << i << " transactions" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Throttle
        }
    }

    // Monitor for a while
    std::cout << "Monitoring blockchain progress..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    bool success = false;
    
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(60)) {
        uint64_t max_height = 0;
        uint64_t min_height = 999999;
        
        for (int i = 0; i < 3; ++i) {
            auto height_bytes = nodes[i]->getBlockchainStorage()->getMetadata(chrono_node::NEXT_BLOCK_HEIGHT_KEY);
            uint64_t height = 0;
            if (height_bytes) {
                size_t offset = 0;
                height = chrono_util::read_fixed_uint64_le(*height_bytes, offset);
            }
            if (height > max_height) max_height = height;
            if (height < min_height) min_height = height;
        }
        
        std::cout << "Current heights: " << min_height << " - " << max_height << std::endl;

        if (min_height > 5) { // If all nodes produced some blocks
            success = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (!success) {
        std::cerr << "Blockchain failed to produce blocks or sync" << std::endl;
    } else {
        std::cout << "SUCCESS: Blockchain produced blocks!" << std::endl;
    }

    // 5. Stop Nodes
    std::cout << "Stopping nodes..." << std::endl;
    for (auto& node : nodes) {
        node->stop();
    }

    for (auto& t : node_threads) {
        if (t.joinable()) t.join();
    }
    
    return success ? 0 : 1;
}