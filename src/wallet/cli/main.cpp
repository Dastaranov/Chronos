#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "crypto/signer_dilithium.hpp"
#include "crypto/key_manager.hpp"
#include "util/bytes.hpp"
#include "util/log.hpp"
#include "address/address.hpp"

void print_usage() {
    std::cout << "Chronos Wallet CLI - Key Management Tool" << std::endl;
    std::cout << "Usage: wallet_cli <command> [options]" << std::endl;
    std::cout << "\nCommands:" << std::endl;
    std::cout << "  generate-keys <key-id>    Generate a new Dilithium key pair and store securely." << std::endl;
    std::cout << "                            Example: wallet_cli generate-keys validator-1" << std::endl;
    std::cout << "  list-keys                 List all stored key IDs." << std::endl;
    std::cout << "  show-public <key-id>      Display the Base58Check-encoded public key." << std::endl;
    std::cout << "\nExamples:" << std::endl;
    std::cout << "  wallet_cli generate-keys validator-1      # Create and store a new key" << std::endl;
    std::cout << "  wallet_cli list-keys                      # See all available keys" << std::endl;
    std::cout << "  wallet_cli show-public validator-1        # Show public key for config" << std::endl;
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

    // === COMMAND: generate-keys ===
    if (command == "generate-keys") {
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

        // Save private key securely to disk
        if (!key_mgr.save_private_key(key_id, private_key)) {
            std::cerr << "Error: Failed to save private key to disk." << std::endl;
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
        std::cout << "Available Keys:" << std::endl;
        std::cout << "═══════════════════════════════════════" << std::endl;
        
        auto keys = key_mgr.list_keys();
        
        if (keys.empty()) {
            std::cout << "No keys found in " << keys_dir << std::endl;
        } else {
            for (size_t i = 0; i < keys.size(); ++i) {
                std::cout << (i + 1) << ". " << keys[i] << std::endl;
            }
        }
        
        std::cout << "═══════════════════════════════════════\n" << std::endl;

    // === COMMAND: show-public ===
    } else if (command == "show-public") {
        if (argc != 3) {
            std::cerr << "Usage: wallet_cli show-public <key-id>" << std::endl;
            return 1;
        }
        
        std::string key_id = argv[2];
        
        // Load private key to reconstruct public key
        chrono_util::Bytes private_key = key_mgr.load_private_key(key_id);
        if (private_key.empty()) {
            std::cerr << "Error: Key '" << key_id << "' not found." << std::endl;
            return 1;
        }
        
        // Recreate signer from private key to get public key
        try {
            chrono_crypto::SignerDilithium signer(private_key);
            chrono_util::Bytes public_key = signer.get_public_key();
            std::string public_key_base58 = key_mgr.encode_public_key_base58(public_key);
            
            std::cout << "Public Key for '" << key_id << "':" << std::endl;
            std::cout << "════════════════════════════════════════════════════════" << std::endl;
            std::cout << public_key_base58 << std::endl;
            std::cout << "════════════════════════════════════════════════════════\n" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error: Failed to load key: " << e.what() << std::endl;
            return 1;
        }

    } else {
        std::cerr << "Unknown command: " << command << std::endl;
        print_usage();
        return 1;
    }

    return 0;
}