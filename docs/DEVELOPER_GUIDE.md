# Chronos Blockchain - Developer Guide

## 1. Architecture Overview
Chronos is a modular C++20 blockchain.

### Core Components
- **NodeApp (`src/node/`)**: The main application class that glues everything together.
- **Consensus (`src/consensus/`)**: Implements the BFT (Byzantine Fault Tolerance) consensus engine and Proof-of-Time (PoT) logic.
- **P2P (`src/p2p/`)**: Handles networking, peer discovery, and message propagation using a custom protocol over TCP.
- **Ledger (`src/ledger/`)**: Manages the blockchain state, blocks, transactions, and account balances.
- **Crypto (`src/crypto/`)**: Handles post-quantum signatures (Dilithium), hashing (BLAKE3), and key management.
- **Storage (`src/storage/`)**: Interfaces for data persistence (LevelDB, RocksDB).

## 2. Build System
We use CMake.

### Dependencies
- **liboqs**: For quantum-safe cryptography.
- **LevelDB**: For efficient storage.
- **nlohmann_json**: For JSON RPC and serialization.
- **cpp-httplib**: For the RPC server.

### Compilation
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```

## 3. Testing
We use a custom test framework located in `tests/test_framework.hpp`.

### Running Tests
```bash
cd build
./chronos_tests
```

### Writing Tests
Create a new `.cpp` file in `tests/` and register it in `CMakeLists.txt`.
```cpp
#include "test_framework.hpp"

TEST_CASE("MyNewFeature") {
    ASSERT_EQ(1 + 1, 2);
}
```

## 4. Code Style
- **Standard**: C++20.
- **Formatting**: Use 4 spaces for indentation.
- **Naming**: Snake_case for variables and functions, CamelCase for classes.
- **Documentation**: Doxygen-style comments in headers.

## 5. Key Workflows

### Adding a New Transaction Type
1. Update `TransactionType` enum in `src/ledger/transaction.hpp`.
2. Update `Transaction::serialize/deserialize`.
3. Implement validation logic in `State::apply_transaction` (`src/ledger/state.cpp`).
4. Add CLI support in `wallet_cli`.

### Modifying Consensus
The BFT logic is in `src/consensus/bft.cpp`. Be extremely careful when modifying the state machine. Always run `test_bft` and `test_robust_integration` after changes.

## 6. Debugging
- Logs are written to `logs/chronos.log` by default via `chrono_util::setup_logging(...)`.
- Use `LOG_INFO`, `LOG_DEBUG`, etc., from `src/util/log.hpp`.
- For complex concurrency issues, use the `test_robust_integration` test with increased logging.

## 7. Contribution
1. Fork the repository.
2. Create a feature branch.
3. Write tests for your changes.
4. Submit a Pull Request.

## 8. Network Bootstrapping
To launch a new network (e.g., a new testnet), you must generate a canonical Genesis Block.
- Use `src/tools/genesis_tool.cpp` to generate the `genesis.json`.
- This file defines the initial state (allocations, validators).
- The hash of this genesis block is embedded in the P2P Handshake. Nodes with mismatching genesis hashes will disconnect.
