#include "node_connector.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iomanip> // Added for setw
#include <limits> // Added for numeric_limits
#include "crypto/signer_dilithium.hpp"
#include "crypto/key_manager.hpp"
#include "util/bytes.hpp"
#include "util/log.hpp"
#include "address/address.hpp"
#include "ledger/transaction.hpp"

#include <termios.h>
#include <unistd.h>

using json = nlohmann::json;

// Global connector instance
NodeConnector connector;

// Helper to securely read passphrase
std::string get_passphrase(const std::string& prompt, bool confirm = false) {
    std::string password;
    struct termios oldt, newt;
    
    std::cout << prompt << std::flush;
    
    // Disable echo
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    
    std::getline(std::cin, password);
    
    // Restore echo
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    std::cout << std::endl;
    
    if (confirm) {
        std::string confirm_pass;
        std::cout << "Confirm passphrase: " << std::flush;
        
        tcgetattr(STDIN_FILENO, &oldt);
        newt = oldt;
        newt.c_lflag &= ~ECHO;
        tcsetattr(STDIN_FILENO, TCSANOW, &newt);
        
        std::getline(std::cin, confirm_pass);
        
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
        std::cout << std::endl;
        
        if (password != confirm_pass) {
            throw std::runtime_error("Passphrases do not match");
        }
    }
    
    return password;
}

void print_usage() {
    std::cout << "Chronos Wallet CLI - Key Management & Transaction Tool" << std::endl;
    std::cout << "Usage: wallet_cli <command> [options]" << std::endl;
    std::cout << "\nKey Management Commands:" << std::endl;
    std::cout << "  generate-keys <key-id>    Generate a new Dilithium key pair and store securely." << std::endl;
    std::cout << "  list-keys                 List all stored key IDs." << std::endl;
    std::cout << "  show-public <key-id>      Display the Base58Check-encoded public key." << std::endl;
    std::cout << "\nNode Interaction Commands:" << std::endl;
    std::cout << "  status                    Get node status." << std::endl;
    std::cout << "  balance <address>         Get balance of an address." << std::endl;
    std::cout << "  send <key-id> <to> <amt>  Send transaction (amount in nanos)." << std::endl;
    std::cout << "  stake <key-id> <amt>      Stake amount to become a validator." << std::endl;
    std::cout << "  unstake <key-id> <amt>    Unstake amount." << std::endl;
    std::cout << "  get-block <height|hash>   Get block details." << std::endl;
    std::cout << "  import-key <key-id> <hex> Import a private key from hex string." << std::endl;
    std::cout << "  export-key <key-id>       Export a private key to hex string (WARNING: SENSITIVE)." << std::endl;
    std::cout << "  discover                  Discover new nodes from current connections." << std::endl;
    std::cout << "  list-nodes                List configured nodes." << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  --rpc <host:port>         Add RPC endpoint (can be used multiple times)" << std::endl;
    std::cout << "  --api-key <key>           Set RPC API key for the last added node" << std::endl;
}

json rpc_call(const std::string& method, const json& params) {
    return connector.rpc_call(method, params);
}

int main(int argc, char* argv[]) {
    // Initialize logging
    LOG_INIT(".");
    
    // Determine key storage directory (default: ~/.chronos/keys)
    std::string home_dir = std::string(std::getenv("HOME") ? std::getenv("HOME") : ".");
    std::string keys_dir = home_dir + "/.chronos/keys";
    std::string config_path = home_dir + "/.chronos/wallet_config.json";

    // Ensure .chronos directory exists
    std::filesystem::create_directories(home_dir + "/.chronos");

    chrono_crypto::KeyManager key_mgr(keys_dir);
    
    // Load existing config
    connector.load_config(config_path);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string command = argv[1];
    int arg_idx = 2;

    // Parse options (simple)
    std::string last_host;
    int last_port = 0;

    while (argc > 2) {
        std::string arg = argv[argc-2];
        if (arg == "--rpc") {
            std::string rpc_endpoint = argv[argc-1];
            size_t colon = rpc_endpoint.find(':');
            if (colon != std::string::npos) {
                last_host = rpc_endpoint.substr(0, colon);
                last_port = std::stoi(rpc_endpoint.substr(colon + 1));
                connector.add_node(last_host, last_port);
            }
            argc -= 2;
        } else if (arg == "--api-key") {
            std::string api_key = argv[argc-1];
            if (!last_host.empty()) {
                connector.add_node(last_host, last_port, api_key);
            }
            argc -= 2;
        } else {
            break;
        }
    }

    // If no nodes configured, add default
    if (connector.get_nodes().empty()) {
        connector.add_node("127.0.0.1", 8080);
    }

    try {
        // === COMMAND: discover ===
        if (command == "discover") {
            std::cout << "Discovering nodes..." << std::endl;
            connector.discover_nodes();
            connector.save_config(config_path);
            std::cout << "Discovery complete. Active nodes: " << connector.get_nodes().size() << std::endl;
            for (const auto& node : connector.get_nodes()) {
                std::cout << "  - " << node.host << ":" << node.port << std::endl;
            }

        // === COMMAND: list-nodes ===
        } else if (command == "list-nodes") {
            auto nodes = connector.get_nodes();
            if (nodes.empty()) std::cout << "No nodes configured." << std::endl;
            for (const auto& node : nodes) {
                std::cout << "- " << node.host << ":" << node.port << (node.api_key.empty() ? "" : " (API Key set)") << std::endl;
            }

        // === COMMAND: generate-keys ===
        } else if (command == "generate-keys") {
            if (argc != 3) {
                std::cerr << "Usage: wallet_cli generate-keys <key-id>" << std::endl;
                return 1;
            }
            
            std::string key_id = argv[2];
            
            // Check if key already exists
            if (key_mgr.key_exists(key_id)) {
                std::cerr << "Error: Key with ID '" << key_id << "' already exists." << std::endl;
                return 1;
            }
            
            chrono_util::Bytes public_key;
            chrono_util::Bytes private_key;

            std::cout << "Generating Dilithium key pair for '" << key_id << "'..." << std::endl;

            // Generate new key pair
            if (!chrono_crypto::SignerDilithium::generate_key_pair(public_key, private_key)) {
                std::cerr << "Error: Failed to generate key pair." << std::endl;
                return 1;
            }

            // Ask for passphrase
            std::string passphrase;
            try {
                passphrase = get_passphrase("Enter passphrase to encrypt key (empty for none): ", true);
            } catch (const std::exception& e) {
                std::cerr << "Error: " << e.what() << std::endl;
                return 1;
            }

            // Save key pair securely to disk
            if (!key_mgr.save_key_pair(key_id, {public_key, private_key}, passphrase)) {
                std::cerr << "Error: Failed to save key pair to disk." << std::endl;
                return 1;
            }

            // Encode public key in Base58Check format
            std::string public_key_base58 = key_mgr.encode_public_key_base58(public_key);
            
            // Create address from public key
            chrono_address::Address address(public_key);

            // Display results
            std::cout << "\n╔════════════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║              Key Pair Generated Successfully                       ║" << std::endl;
            std::cout << "╚════════════════════════════════════════════════════════════════════╝" << std::endl;
            
            std::cout << "\n[✓] Key ID: " << key_id << std::endl;
            std::cout << "    Location: " << keys_dir << "/" << key_id << ".key" << std::endl;
            std::cout << "    Status: Securely stored (readable by owner only)" << std::endl;
            
            std::cout << "\n[+] Public Key (Base58Check - for validators list):" << std::endl;
            std::cout << "    " << public_key_base58 << std::endl;
            
            std::cout << "\n[+] Public Key (Full HEX - for reference):" << std::endl;
            std::cout << "    " << chrono_util::bytes_to_hex(public_key).substr(0, 80) << "..." << std::endl;
            
            std::cout << "\n[+] Address (Bech32):" << std::endl;
            std::cout << "    " << address.to_string() << std::endl;

            std::cout << "\n[!] Private Key:" << std::endl;
            std::cout << "    Status: Securely stored on disk" << std::endl;
            std::cout << "    Access: Use 'private_key_id = \"" << key_id << "\"' in config.toml" << std::endl;
            
            std::cout << "\n╔════════════════════════════════════════════════════════════════════╗" << std::endl;
            std::cout << "║                          Configuration                             ║" << std::endl;
            std::cout << "╚════════════════════════════════════════════════════════════════════╝" << std::endl;
            
            std::cout << "\n1. Add to [crypto] section in config.toml:" << std::endl;
            std::cout << "   private_key_id = \"" << key_id << "\"" << std::endl;
            
            std::cout << "\n2. Add to [consensus] validators list in config.toml:" << std::endl;
            std::cout << "   validators = [\"" << public_key_base58 << "\"]" << std::endl;
            
            std::cout << "\n3. Ensure ~/.chronos/keys/ directory exists (automatically created)" << std::endl;
            
            std::cout << "\n════════════════════════════════════════════════════════════════════\n" << std::endl;

        // === COMMAND: list-keys ===
        } else if (command == "list-keys") {
            auto keys = key_mgr.list_keys();
            if (keys.empty()) std::cout << "No keys found." << std::endl;
            for (const auto& k : keys) std::cout << "- " << k << std::endl;

        // === COMMAND: show-public ===
        } else if (command == "show-public") {
            if (argc != 3) {
                std::cerr << "Usage: wallet_cli show-public <key-id>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            
            std::string passphrase = get_passphrase("Enter passphrase: ");
            
            auto key_pair = key_mgr.load_key_pair(key_id, passphrase);
            if (key_pair) {
                std::cout << key_mgr.encode_public_key_base58(key_pair->public_key) << std::endl;
            } else {
                // Fallback to private key only (legacy)
                chrono_util::Bytes private_key = key_mgr.load_private_key(key_id, passphrase);
                if (private_key.empty()) {
                    std::cerr << "Error: Key '" << key_id << "' not found or wrong passphrase." << std::endl;
                    return 1;
                }
                chrono_crypto::SignerDilithium signer(private_key);
                std::cout << key_mgr.encode_public_key_base58(signer.get_public_key()) << std::endl;
                std::cerr << "WARNING: Loaded legacy key. Public key might be incorrect if not migrated." << std::endl;
            }

        // === COMMAND: status ===
        } else if (command == "status") {
            json res = rpc_call("get_status", json::object());
            std::cout << res.dump(2) << std::endl;

        // === COMMAND: balance ===
        } else if (command == "balance") {
            if (argc != 3) {
                std::cerr << "Usage: wallet_cli balance <address>" << std::endl;
                return 1;
            }
            json params;
            params["address"] = argv[2];
            json res = rpc_call("get_balance", params);
            if (res["status"] == "success") {
                std::cout << "Balance: " << res["balance"] << " nanos" << std::endl;
            } else {
                std::cerr << "Error: " << res["message"] << std::endl;
            }

        // === COMMAND: get-block ===
        } else if (command == "get-block") {
            if (argc != 3) {
                std::cerr << "Usage: wallet_cli get-block <height|hash>" << std::endl;
                return 1;
            }
            std::string arg = argv[2];
            json params;
            if (arg.length() == 64) { // Assume hash
                params["hash"] = arg;
            } else {
                params["height"] = std::stoull(arg);
            }
            json res = rpc_call("get_block", params);
            std::cout << res.dump(2) << std::endl;

        // === COMMAND: send ===
        } else if (command == "send") {
            if (argc != 5) {
                std::cerr << "Usage: wallet_cli send <key-id> <to-address> <amount>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            std::string to_addr_str = argv[3];
            uint64_t amount = std::stoull(argv[4]);

            // 1. Load key
            std::string passphrase = get_passphrase("Enter passphrase: ");
            
            std::unique_ptr<chrono_crypto::SignerDilithium> signer;
            auto key_pair = key_mgr.load_key_pair(key_id, passphrase);
            
            if (key_pair) {
                signer = std::make_unique<chrono_crypto::SignerDilithium>(key_pair->public_key, key_pair->private_key);
            } else {
                chrono_util::Bytes private_key = key_mgr.load_private_key(key_id, passphrase);
                if (private_key.empty()) {
                    std::cerr << "Error: Key '" << key_id << "' not found or wrong passphrase." << std::endl;
                    return 1;
                }
                signer = std::make_unique<chrono_crypto::SignerDilithium>(private_key);
                std::cerr << "WARNING: Loaded legacy key. Transaction might fail verification." << std::endl;
            }
            
            chrono_address::Address from_addr(signer->get_public_key());
            
            // 2. Get nonce
            json nonce_params;
            nonce_params["address"] = from_addr.to_string();
            json nonce_res = rpc_call("get_nonce", nonce_params);
            if (nonce_res["status"] != "success") {
                std::cerr << "Error fetching nonce: " << nonce_res["message"] << std::endl;
                return 1;
            }
            uint64_t nonce = nonce_res["nonce"];

            // 3. Create transaction
            chrono_address::Address to_addr(to_addr_str);
            uint64_t fee = 100; // Default fee
            chrono_ledger::Transaction tx(from_addr, to_addr, amount, fee, nonce, signer->get_public_key());
            
            // 4. Sign
            tx.signature = signer->sign_message(tx.get_hash_for_signing());

            // 5. Send
            json send_params;
            send_params["sender_pub_key"] = chrono_util::bytes_to_hex(signer->get_public_key());
            send_params["recipient_address"] = to_addr_str;
            send_params["amount"] = amount;
            send_params["fee"] = fee;
            send_params["nonce"] = nonce;
            send_params["signature"] = chrono_util::bytes_to_hex(tx.signature);

            json send_res = rpc_call("send_transaction", send_params);
            std::cout << send_res.dump(2) << std::endl;

        // === COMMAND: stake ===
        } else if (command == "stake") {
            if (argc < 4) {
                std::cerr << "Usage: wallet_cli stake <key-id> <amount> [time-tier] [name]" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            uint64_t amount = std::stoull(argv[3]);
            uint8_t time_tier = 5;
            std::string name;
            
            if (argc > 4) time_tier = std::stoi(argv[4]);
            if (argc > 5) name = argv[5];

            // 1. Load key
            std::string passphrase = get_passphrase("Enter passphrase: ");
            auto key_pair = key_mgr.load_key_pair(key_id, passphrase);
            if (!key_pair) {
                std::cerr << "Error: Key not found or wrong passphrase." << std::endl;
                return 1;
            }
            std::unique_ptr<chrono_crypto::SignerDilithium> signer = std::make_unique<chrono_crypto::SignerDilithium>(key_pair->public_key, key_pair->private_key);
            
            chrono_address::Address from_addr(signer->get_public_key());
            
            // 2. Get nonce
            json nonce_params;
            nonce_params["address"] = from_addr.to_string();
            json nonce_res = rpc_call("get_nonce", nonce_params);
            if (nonce_res["status"] != "success") {
                std::cerr << "Error fetching nonce: " << nonce_res["message"] << std::endl;
                return 1;
            }
            uint64_t nonce = nonce_res["nonce"];

            // 3. Create transaction
            // Recipient is same as sender for stake registration (self-stake)
            uint64_t fee = 100; 
            
            // Payload: [time_tier] + [name]
            chrono_util::Bytes payload;
            payload.push_back(time_tier);
            payload.insert(payload.end(), name.begin(), name.end());

            chrono_ledger::Transaction tx(from_addr, from_addr, amount, fee, nonce, signer->get_public_key(), chrono_ledger::TransactionType::STAKE_REGISTRATION, payload);
            
            // 4. Sign
            tx.signature = signer->sign_message(tx.get_hash_for_signing());

            // 5. Send
            json send_params;
            send_params["sender_pub_key"] = chrono_util::bytes_to_hex(signer->get_public_key());
            send_params["recipient_address"] = from_addr.to_string();
            send_params["amount"] = amount;
            send_params["fee"] = fee;
            send_params["nonce"] = nonce;
            send_params["signature"] = chrono_util::bytes_to_hex(tx.signature);
            send_params["type"] = static_cast<int>(chrono_ledger::TransactionType::STAKE_REGISTRATION);
            send_params["payload"] = chrono_util::bytes_to_hex(payload);

            json send_res = rpc_call("send_transaction", send_params);
            std::cout << send_res.dump(2) << std::endl;

        // === COMMAND: unstake ===
        } else if (command == "unstake") {
            if (argc != 4) {
                std::cerr << "Usage: wallet_cli unstake <key-id> <amount>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            uint64_t amount = std::stoull(argv[3]);

            // 1. Load key
            std::string passphrase = get_passphrase("Enter passphrase: ");
            auto key_pair = key_mgr.load_key_pair(key_id, passphrase);
            if (!key_pair) {
                std::cerr << "Error: Key not found or wrong passphrase." << std::endl;
                return 1;
            }
            std::unique_ptr<chrono_crypto::SignerDilithium> signer = std::make_unique<chrono_crypto::SignerDilithium>(key_pair->public_key, key_pair->private_key);
            
            chrono_address::Address from_addr(signer->get_public_key());
            
            // 2. Get nonce
            json nonce_params;
            nonce_params["address"] = from_addr.to_string();
            json nonce_res = rpc_call("get_nonce", nonce_params);
            if (nonce_res["status"] != "success") {
                std::cerr << "Error fetching nonce: " << nonce_res["message"] << std::endl;
                return 1;
            }
            uint64_t nonce = nonce_res["nonce"];

            // 3. Create transaction
            uint64_t fee = 100; 
            chrono_ledger::Transaction tx(from_addr, from_addr, amount, fee, nonce, signer->get_public_key(), chrono_ledger::TransactionType::UNSTAKE);
            
            // 4. Sign
            tx.signature = signer->sign_message(tx.get_hash_for_signing());

            // 5. Send
            json send_params;
            send_params["sender_pub_key"] = chrono_util::bytes_to_hex(signer->get_public_key());
            send_params["recipient_address"] = from_addr.to_string();
            send_params["amount"] = amount;
            send_params["fee"] = fee;
            send_params["nonce"] = nonce;
            send_params["signature"] = chrono_util::bytes_to_hex(tx.signature);
            send_params["type"] = static_cast<int>(chrono_ledger::TransactionType::UNSTAKE);

            json send_res = rpc_call("send_transaction", send_params);
            std::cout << send_res.dump(2) << std::endl;

        // === COMMAND: import-key ===
        } else if (command == "import-key") {
            if (argc != 5) { // Changed to 5
                std::cerr << "Usage: wallet_cli import-key <key-id> <private-key-hex> <public-key-hex>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            std::string hex_priv = argv[3];
            std::string hex_pub = argv[4];
            
            try {
                chrono_util::Bytes private_key = chrono_util::hex_to_bytes(hex_priv);
                chrono_util::Bytes public_key = chrono_util::hex_to_bytes(hex_pub);
                
                std::string passphrase;
                try {
                    passphrase = get_passphrase("Enter passphrase to encrypt key (empty for none): ", true);
                } catch (const std::exception& e) {
                    std::cerr << "Error: " << e.what() << std::endl;
                    return 1;
                }

                if (key_mgr.save_key_pair(key_id, {public_key, private_key}, passphrase)) {
                    std::cout << "Key '" << key_id << "' imported successfully." << std::endl;
                } else {
                    std::cerr << "Error: Failed to import key (might already exist)." << std::endl;
                    return 1;
                }
            } catch (const std::exception& e) {
                std::cerr << "Error: Invalid hex string." << std::endl;
                return 1;
            }

        // === COMMAND: export-key ===
        } else if (command == "export-key") {
            if (argc != 3) {
                std::cerr << "Usage: wallet_cli export-key <key-id>" << std::endl;
                return 1;
            }
            std::string key_id = argv[2];
            
            std::cout << "WARNING: You are about to display your private key in plain text." << std::endl;
            std::cout << "Are you sure? (y/N): ";
            char confirm;
            std::cin >> confirm;
            // Clear newline from buffer
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            
            if (confirm != 'y' && confirm != 'Y') {
                std::cout << "Aborted." << std::endl;
                return 0;
            }

            std::string passphrase = get_passphrase("Enter passphrase: ");
            
            chrono_util::Bytes key = key_mgr.load_private_key(key_id, passphrase);
            if (!key.empty()) {
                std::cout << "Private Key (HEX): " << chrono_util::bytes_to_hex(key) << std::endl;
            } else {
                std::cerr << "Error: Key not found or wrong passphrase." << std::endl;
                return 1;
            }

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
