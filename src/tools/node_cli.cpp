#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iomanip>
#include <random>
#include <sstream>
#include "crypto/signer_dilithium.hpp"
#include "crypto/key_manager.hpp"
#include "util/bytes.hpp"
#include "util/log.hpp"
#include "address/address.hpp"
#include "ledger/transaction.hpp"

using json = nlohmann::json;

// Default RPC URL
std::string rpc_host = "127.0.0.1";
int rpc_port = 8080;
std::string rpc_api_key = "";

void print_usage() {
    std::cout << "Chronos Node CLI - Node Management & Governance Tool" << std::endl;
    std::cout << "Usage: node_cli <command> [options]" << std::endl;
    std::cout << "\nNode Management Commands:" << std::endl;
    std::cout << "  status                    Get node status." << std::endl;
    std::cout << "  peers                     List connected peers." << std::endl;
    std::cout << "  mempool                   List transactions in mempool." << std::endl;
    std::cout << "  stake <key-id> <amt>      Stake amount to become a validator." << std::endl;
    std::cout << "  unstake <key-id> <amt>    Unstake amount." << std::endl;
    std::cout << "  vote <key-id> <candidate> <approve|reject> Vote for a validator candidate." << std::endl;
    std::cout << "  list-candidates           List validator candidates and voting status." << std::endl;
    std::cout << "  list-keys                 List available keys (for signing)." << std::endl;
    std::cout << "  generate-api-key          Generate a secure random API key." << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --rpc <host:port>         Set RPC endpoint (default: 127.0.0.1:8080)" << std::endl;
    std::cout << "  --api-key <key>           Set RPC API key" << std::endl;
}

json rpc_call(const std::string& method, const json& params) {
    httplib::Client cli(rpc_host, rpc_port);
    
    if (!rpc_api_key.empty()) {
        cli.set_default_headers({{"Authorization", "Bearer " + rpc_api_key}});
    }

    json req;
    req["jsonrpc"] = "2.0";
    req["method"] = method;
    req["params"] = params;
    req["id"] = 1;

    auto res = cli.Post("/", req.dump(), "application/json");
    if (!res || res->status != 200) {
        throw std::runtime_error("RPC connection failed");
    }

    json response = json::parse(res->body);
    if (response.contains("error")) {
        throw std::runtime_error(response["error"]["message"].get<std::string>());
    }
    return response["result"];
}

int main(int argc, char* argv[]) {
    // Initialize logging
    LOG_INIT(".");
    
    // Determine key storage directory (default: ~/.chronos/keys)
    std::string keys_dir = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.chronos/keys";
    chrono_crypto::KeyManager key_mgr(keys_dir);
    
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];

    // Parse options (simple)
    while (argc > 2) {
        std::string arg = argv[argc-2];
        if (arg == "--rpc") {
            std::string rpc_endpoint = argv[argc-1];
            size_t colon = rpc_endpoint.find(':');
            if (colon != std::string::npos) {
                rpc_host = rpc_endpoint.substr(0, colon);
                rpc_port = std::stoi(rpc_endpoint.substr(colon + 1));
            }
            argc -= 2;
        } else if (arg == "--api-key") {
            rpc_api_key = argv[argc-1];
            argc -= 2;
        } else {
            break;
        }
    }

    try {
        // === COMMAND: status ===
        if (command == "status") {
            json res = rpc_call("get_status", json::object());
            std::cout << res.dump(2) << std::endl;

        // === COMMAND: peers ===
        } else if (command == "peers") {
            json res = rpc_call("get_peers", json::object());
            if (res["status"] == "success") {
                auto peers = res["result"];
                if (peers.empty()) {
                    std::cout << "No connected peers." << std::endl;
                } else {
                    std::cout << std::left << std::setw(25) << "Node ID" 
                              << std::setw(25) << "Address" 
                              << std::setw(10) << "Height" 
                              << std::setw(10) << "Score" 
                              << std::setw(15) << "Last Seen (s)" << std::endl;
                    std::cout << std::string(85, '-') << std::endl;
                    
                    for (const auto& p : peers) {
                        std::string node_id = p["node_id"].get<std::string>();
                        if (node_id.length() > 23) node_id = node_id.substr(0, 20) + "...";
                        
                        std::cout << std::left << std::setw(25) << node_id
                                  << std::setw(25) << p["address"].get<std::string>()
                                  << std::setw(10) << p["current_block_height"].get<uint64_t>()
                                  << std::setw(10) << p["score"].get<int>()
                                  << std::setw(15) << p["last_seen_seconds_ago"].get<int64_t>() << std::endl;
                    }
                }
            } else {
                std::cerr << "Error: " << res["message"] << std::endl;
            }

        // === COMMAND: mempool ===
        } else if (command == "mempool") {
            json res = rpc_call("get_mempool", json::object());
            if (res["status"] == "success") {
                auto txs = res["result"];
                if (txs.empty()) {
                    std::cout << "Mempool is empty." << std::endl;
                } else {
                    std::cout << "Mempool contains " << txs.size() << " transactions." << std::endl;
                    for (const auto& tx : txs) {
                        std::cout << "- Hash: " << tx["hash"].get<std::string>() 
                                  << " | Fee: " << tx["fee"].get<uint64_t>() 
                                  << " | Nonce: " << tx["nonce"].get<uint64_t>() << std::endl;
                    }
                }
            } else {
                std::cerr << "Error: " << res["message"] << std::endl;
            }

        // === COMMAND: list-keys ===
        } else if (command == "list-keys") {
            auto keys = key_mgr.list_keys();
            if (keys.empty()) std::cout << "No keys found." << std::endl;
            for (const auto& k : keys) std::cout << "- " << k << std::endl;

        // === COMMAND: stake ===
        } else if (command == "stake") {
            if (argc != 4) {
                std::cerr << "Usage: node_cli stake <key-id> <amount>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            uint64_t amount = std::stoull(argv[3]);

            // 1. Load key
            chrono_util::Bytes private_key = key_mgr.load_private_key(key_id);
            if (private_key.empty()) {
                std::cerr << "Error: Key '" << key_id << "' not found." << std::endl;
                return 1;
            }
            chrono_crypto::SignerDilithium signer(private_key);
            chrono_address::Address from_addr(signer.get_public_key());
            
            // 2. Get nonce
            json nonce_params;
            nonce_params["address"] = from_addr.to_string();
            json nonce_res = rpc_call("get_nonce", nonce_params);
            if (nonce_res["status"] != "success") {
                std::cerr << "Error fetching nonce: " << nonce_res["message"] << std::endl;
                return 1;
            }
            uint64_t nonce = nonce_res["nonce"];

            // 3. Create transaction (Self-transfer with STAKE_REGISTRATION type)
            uint64_t fee = 100; // Default fee
            chrono_ledger::Transaction tx(from_addr, from_addr, amount, fee, nonce, signer.get_public_key(), chrono_ledger::TransactionType::STAKE_REGISTRATION);
            
            // 4. Sign
            tx.signature = signer.sign_message(tx.get_hash_for_signing());

            // 5. Send
            json send_params;
            send_params["sender_pub_key"] = chrono_util::bytes_to_hex(signer.get_public_key());
            send_params["recipient_address"] = from_addr.to_string();
            send_params["amount"] = amount;
            send_params["fee"] = fee;
            send_params["nonce"] = nonce;
            send_params["signature"] = chrono_util::bytes_to_hex(tx.signature);
            send_params["type"] = static_cast<int>(chrono_ledger::TransactionType::STAKE_REGISTRATION);

            json send_res = rpc_call("send_transaction", send_params);
            std::cout << send_res.dump(2) << std::endl;

        // === COMMAND: vote ===
        } else if (command == "vote") {
            if (argc != 5) {
                std::cerr << "Usage: node_cli vote <key-id> <candidate-address> <approve|reject>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            std::string candidate_addr_str = argv[3];
            std::string vote_type_str = argv[4];
            
            bool approve = false;
            if (vote_type_str == "approve") approve = true;
            else if (vote_type_str == "reject") approve = false;
            else {
                std::cerr << "Error: Vote type must be 'approve' or 'reject'." << std::endl;
                return 1;
            }

            // 1. Load key
            chrono_util::Bytes private_key = key_mgr.load_private_key(key_id);
            if (private_key.empty()) {
                std::cerr << "Error: Key '" << key_id << "' not found." << std::endl;
                return 1;
            }
            chrono_crypto::SignerDilithium signer(private_key);
            chrono_address::Address from_addr(signer.get_public_key());
            
            // 2. Get nonce
            json nonce_params;
            nonce_params["address"] = from_addr.to_string();
            json nonce_res = rpc_call("get_nonce", nonce_params);
            if (nonce_res["status"] != "success") {
                std::cerr << "Error fetching nonce: " << nonce_res["message"] << std::endl;
                return 1;
            }
            uint64_t nonce = nonce_res["nonce"];

            // 3. Create transaction (VOTE type)
            uint64_t amount = 0;
            uint64_t fee = 100; 
            chrono_address::Address candidate_addr(candidate_addr_str);
            
            uint64_t vote_value = approve ? 1 : 0;
            
            chrono_ledger::Transaction tx(from_addr, candidate_addr, vote_value, fee, nonce, signer.get_public_key(), chrono_ledger::TransactionType::VOTE);
            
            // 4. Sign
            tx.signature = signer.sign_message(tx.get_hash_for_signing());

            // 5. Send
            json send_params;
            send_params["sender_pub_key"] = chrono_util::bytes_to_hex(signer.get_public_key());
            send_params["recipient_address"] = candidate_addr_str;
            send_params["amount"] = vote_value; 
            send_params["fee"] = fee;
            send_params["nonce"] = nonce;
            send_params["signature"] = chrono_util::bytes_to_hex(tx.signature);
            send_params["type"] = static_cast<int>(chrono_ledger::TransactionType::VOTE);

            json send_res = rpc_call("send_transaction", send_params);
            std::cout << send_res.dump(2) << std::endl;

        // === COMMAND: list-candidates ===
        } else if (command == "list-candidates") {
            json res = rpc_call("get_candidates", json::object());
            if (res["status"] == "success") {
                auto candidates = res["result"];
                if (candidates.empty()) {
                    std::cout << "No pending candidates found." << std::endl;
                } else {
                    std::cout << std::left << std::setw(25) << "Node ID" 
                              << std::setw(15) << "Name" 
                              << std::setw(15) << "Stake" 
                              << std::setw(10) << "For" 
                              << std::setw(10) << "Against" 
                              << std::setw(10) << "Required" << std::endl;
                    std::cout << std::string(95, '-') << std::endl;
                    
                    for (const auto& c : candidates) {
                        std::string node_id = c["node_id"].get<std::string>();
                        if (node_id.length() > 23) node_id = node_id.substr(0, 20) + "...";
                        
                        std::cout << std::left << std::setw(25) << node_id
                                  << std::setw(15) << c["name"].get<std::string>()
                                  << std::setw(15) << c["stake_nanos"].get<uint64_t>()
                                  << std::setw(10) << c["votes_for"].get<size_t>()
                                  << std::setw(10) << c["votes_against"].get<size_t>()
                                  << std::setw(10) << c["votes_required"].get<size_t>() << std::endl;
                    }
                }
            } else {
                std::cerr << "Error: " << res["message"] << std::endl;
            }

        // === COMMAND: unstake ===
        } else if (command == "unstake") {
            if (argc != 4) {
                std::cerr << "Usage: node_cli unstake <key-id> <amount>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            uint64_t amount = std::stoull(argv[3]);

            // 1. Load key
            chrono_util::Bytes private_key = key_mgr.load_private_key(key_id);
            if (private_key.empty()) {
                std::cerr << "Error: Key '" << key_id << "' not found." << std::endl;
                return 1;
            }
            chrono_crypto::SignerDilithium signer(private_key);
            chrono_address::Address from_addr(signer.get_public_key());
            
            // 2. Get nonce
            json nonce_params;
            nonce_params["address"] = from_addr.to_string();
            json nonce_res = rpc_call("get_nonce", nonce_params);
            if (nonce_res["status"] != "success") {
                std::cerr << "Error fetching nonce: " << nonce_res["message"] << std::endl;
                return 1;
            }
            uint64_t nonce = nonce_res["nonce"];

            // 3. Create transaction (Self-transfer with UNSTAKE type)
            uint64_t fee = 100; // Default fee
            chrono_ledger::Transaction tx(from_addr, from_addr, amount, fee, nonce, signer.get_public_key(), chrono_ledger::TransactionType::UNSTAKE);
            
            // 4. Sign
            tx.signature = signer.sign_message(tx.get_hash_for_signing());

            // 5. Send
            json send_params;
            send_params["sender_pub_key"] = chrono_util::bytes_to_hex(signer.get_public_key());
            send_params["recipient_address"] = from_addr.to_string();
            send_params["amount"] = amount;
            send_params["fee"] = fee;
            send_params["nonce"] = nonce;
            send_params["signature"] = chrono_util::bytes_to_hex(tx.signature);
            send_params["type"] = static_cast<int>(chrono_ledger::TransactionType::UNSTAKE);

            json send_res = rpc_call("send_transaction", send_params);
            std::cout << send_res.dump(2) << std::endl;

        // === COMMAND: generate-api-key ===
        } else if (command == "generate-api-key") {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, 15);
            std::stringstream ss;
            for (int i = 0; i < 32; ++i) {
                ss << std::hex << dis(gen);
            }
            std::string key = ss.str();
            std::cout << "Generated API Key: " << key << std::endl;
            std::cout << "Add this to your config.toml under [rpc]:" << std::endl;
            std::cout << "api_key = \"" << key << "\"" << std::endl;

        } else {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage();
            return 1;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
