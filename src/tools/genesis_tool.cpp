#include "ledger/block.hpp"
#include "util/bytes.hpp"
#include "util/log.hpp"
#include <iostream>
#include <chrono>


int main(int argc, char* argv[]) {
    // Default consensus time (can be overridden)
    uint64_t consensus_time = 0;
    if (argc > 1) {
        consensus_time = std::stoull(argv[1]);
    } else {
        // Use current time if not provided
        consensus_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
    }

    chrono_util::Bytes zero_hash(32, 0);
    chrono_ledger::Block genesis_block(zero_hash, 0, consensus_time, 0, 1, 100, {}); // Tier 1, Score 100 for genesis
    
    // Calculate hash
    chrono_util::Bytes hash = genesis_block.get_header_hash();
    
    std::cout << "Genesis Block Details:" << std::endl;
    std::cout << "----------------------" << std::endl;
    std::cout << "Consensus Time: " << consensus_time << std::endl;
    std::cout << "Prev Hash:      " << chrono_util::bytes_to_hex(zero_hash) << std::endl;
    std::cout << "Merkle Root:    " << chrono_util::bytes_to_hex(genesis_block.transactions_merkle_root) << std::endl;
    std::cout << "Genesis Hash:   " << chrono_util::bytes_to_hex(hash) << std::endl;
    std::cout << "----------------------" << std::endl;
    std::cout << "Update your config.toml with:" << std::endl;
    std::cout << "[genesis]" << std::endl;
    std::cout << "consensus_time = " << consensus_time << std::endl;
    std::cout << "expected_hash = \"" << chrono_util::bytes_to_hex(hash) << "\"" << std::endl;

    return 0;
}
