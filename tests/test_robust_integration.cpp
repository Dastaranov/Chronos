#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <memory>
#include <filesystem>
#include <algorithm>

#include "node/node_app.hpp"
#include "node/config.hpp"
#include "crypto/key_manager.hpp"
#include "crypto/signer_dilithium.hpp" // Added include
#include "util/log.hpp"
#include "ledger/transaction.hpp"
#include "util/bytes.hpp"

using namespace chronos;

// Helper to create a test configuration
chrono_node::Config create_test_config(int id, int port, const std::string& data_dir) {
    chrono_node::Config cfg;
    cfg.data_dir = data_dir;
    cfg.listen_addr = "127.0.0.1";
    cfg.listen_port = port;
    cfg.rpc_port = port + 1000;
    cfg.node_type = chrono_node::NodeType::FULL;
    cfg.storage_backend = "disk"; // Use disk for stability in test
    cfg.bft_round_timeout_ms = 3000; // Increased timeout to avoid premature round changes
    cfg.min_peers = 2; // Reduced for 3-node test
    cfg.max_peers = 20;
    cfg.peer_discovery_interval_ms = 1000;
    cfg.peer_management_interval_ms = 1000; // Retry connections every second
    cfg.enable_peer_discovery = true;
    cfg.genesis_consensus_time = 1000000; // Fixed time for genesis
    cfg.min_stake_nanos = 1000;
    cfg.max_total_supply = 1000000000;
    cfg.max_supply_per_account = 1000000000;
    cfg.minting_enabled = true;
    cfg.initial_block_reward_nanos = 50;
    cfg.reward_halving_interval = 1000;
    cfg.fee_burn_percentage = 50;
    
    // Ensure directories exist
    std::filesystem::create_directories(data_dir);
    std::filesystem::create_directories(data_dir + "/keys");
    
    return cfg;
}

int main() {
    // chrono_util::Logger::get_instance().set_log_level(chrono_util::LogLevel::INFO); // Removed
    chrono_util::Logger::get_instance().init("test_robust_integration.log");

    std::cout << "Starting Robust Integration Test..." << std::endl;

    const int NUM_NODES = 3;
    const int BASE_PORT = 9000;
    std::string test_root = "test_data/robust_integration";

    // Clean up previous test data
    std::filesystem::remove_all(test_root);

    std::vector<std::shared_ptr<chrono_node::NodeApp>> nodes;
    std::vector<std::thread> node_threads;
    std::vector<std::pair<std::string, std::string>> node_keys; // (pub, priv) hex
    std::vector<std::string> node_addresses;
    std::vector<chrono_util::Bytes> node_pubkey_bytes;

    // 1. Generate Keys for all nodes
    std::cout << "Generating keys..." << std::endl;
    for (int i = 0; i < NUM_NODES; ++i) {
        std::string key_dir = test_root + "/node_" + std::to_string(i) + "/keys";
        std::filesystem::create_directories(key_dir);
        
        chrono_util::Bytes pub, priv;
        chrono_crypto::SignerDilithium::generate_key_pair(pub, priv); // Use static generation
        
        // Save keys using KeyManager
        chrono_crypto::KeyManager km(key_dir);
        chrono_crypto::KeyManager::KeyPair kp{pub, priv};
        km.save_key_pair("validator", kp);
        
        node_keys.push_back({chrono_util::bytes_to_hex(pub), chrono_util::bytes_to_hex(priv)});
        
        chrono_address::Address addr(pub);
        node_addresses.push_back(addr.to_string());
        node_pubkey_bytes.push_back(pub);
        
        std::cout << "Node " << i << ": " << addr.to_string() << std::endl;
    }

    // 2. Configure and Initialize Nodes
    for (int i = 0; i < NUM_NODES; ++i) {
        std::string data_dir = test_root + "/node_" + std::to_string(i);
        auto cfg = create_test_config(i, BASE_PORT + i, data_dir);
        
        // Set keys
        cfg.public_key = node_keys[i].first;
        cfg.private_key = node_keys[i].second;
        
        // Set validators (all nodes)
        for (const auto& addr : node_addresses) {
            cfg.validators.push_back(addr);
        }
        
        // Set seeds (connect to all other nodes)
        for (int j = 0; j < NUM_NODES; ++j) {
            if (i == j) continue;
            cfg.network_seeds.push_back("127.0.0.1:" + std::to_string(BASE_PORT + j));
        }
        
        // Genesis allocation - Give everyone funds in every node's config
        for (const auto& addr : node_addresses) {
            cfg.genesis_allocations[addr] = 1000000;
        }

        auto node = std::make_shared<chrono_node::NodeApp>(cfg);
        
        // Set time tier to 4 (NTP) for testing, so blocks are valid
        node->set_time_tier_for_testing(4);
        
        // CRITICAL: Populate State's NodeRegistry with ALL validators so they can verify each other's signatures
        auto& registry = node->getState().get_node_registry_mutable();
        for (int j = 0; j < NUM_NODES; ++j) {
            // Use register_node directly with time_tier = 4
            registry.register_node(node_addresses[j], node_keys[j].first, "Node" + std::to_string(j), 10000, 4);
        }
        
        nodes.push_back(node);
    }

    // 3. Start Nodes
    std::cout << "Starting nodes..." << std::endl;
    for (int i = 0; i < NUM_NODES; ++i) {
        auto node = nodes[i];
        node_threads.emplace_back([node]() {
            node->run();
        });
        // Small delay to allow binding
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // 4. Wait for Connections
    std::cout << "Waiting for connections..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 5. Monitor Block Production
    std::cout << "Monitoring block production..." << std::endl;
    std::this_thread::sleep_for(std::chrono::seconds(20));

    // 6. Submit Transactions
    std::cout << "Submitting transactions..." << std::endl;
    // Node 0 sends to Node 1
    chrono_ledger::Transaction tx;
    tx.sender = chrono_address::Address(node_addresses[0]);
    tx.recipient = chrono_address::Address(node_addresses[1]);
    tx.amount = 100;
    tx.fee = 10;
    tx.nonce = nodes[0]->getState().get_nonce(node_addresses[0]); // Use current nonce (0 initially)
    // Removed timestamp
    tx.public_key = node_pubkey_bytes[0];
    
    // Sign
    chrono_crypto::SignerDilithium signer(node_pubkey_bytes[0], chrono_util::hex_to_bytes(node_keys[0].second));
    tx.signature = signer.sign_message(tx.get_hash_for_signing());
    
    nodes[0]->add_transaction(tx);
    std::cout << "Transaction submitted: " << tx.to_string() << std::endl;

    // Wait for inclusion
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // 7. Stop Nodes
    std::cout << "Stopping nodes..." << std::endl;
    for (auto& node : nodes) {
        node->stop();
    }

    for (auto& t : node_threads) {
        if (t.joinable()) t.join();
    }

    std::cout << "Test completed." << std::endl;
    return 0;
}
