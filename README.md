# Chronos: A C++20 Blockchain Node Implementation

Chronos is a cutting-edge blockchain node implementation developed in C++20, designed with a strong emphasis on modularity, performance, and future-readiness. It incorporates modern cryptographic standards, including post-quantum cryptography, and features a robust peer-to-peer network and a Byzantine Fault Tolerant (BFT) consensus mechanism.

## Key Features

*   **C++20 Standard:** Leverages modern C++20 features for improved code quality, performance, and developer experience.
*   **Modular Architecture:** Components like consensus, P2P networking, ledger management, and cryptography are designed as distinct, pluggable modules.
*   **Protobuf for P2P Communication:** Utilizes Google Protocol Buffers for efficient, language-agnostic binary serialization of P2P messages, ensuring high-throughput and low-latency communication.
*   **Pluggable Cryptography:** Supports standard cryptographic primitives and is engineered for post-quantum cryptography (PQC) readiness, with an optional integration of Dilithium via the `liboqs` library.
*   **TOML-based Configuration:** Easy-to-manage node settings through TOML configuration files (e.g., `config/default.toml`).
*   **CMake-driven Build System:** Dependencies are managed efficiently via CMake's `FetchContent` module, simplifying the build process.
*   **Proof-of-Time (PoT) and Byzantine Fault Tolerant (BFT) Consensus:** Implements a robust consensus mechanism to ensure network security and agreement on the blockchain state.

## Building and Running

### Prerequisites

Ensure you have the following installed:

*   **C++20 Compatible Compiler:** GCC 10+ or Clang 12+.
*   **CMake:** Version 3.18 or higher.
*   **Protobuf:** `libprotoc` version 3.21.12 or higher.
*   **Optional - liboqs:** For Post-Quantum Cryptography (Dilithium support).

You can check your versions with:
```bash
cmake --version
protoc --version
gcc --version # or clang++ --version
```

### Building

1.  **Clone the repository:**
    ```bash
git clone https://github.com/Dastaranov/Chronos
cd Chronos
```

2.  **Configure with CMake:**
    Create a build directory and run CMake from there.
    ```bash
mkdir build
cd build
cmake ..
```
    **For Post-Quantum Cryptography (Dilithium via liboqs):**
    If you have `liboqs` installed, you can enable Dilithium support by providing its include and library paths during CMake configuration:
    ```bash
cmake .. -DCHRONOS_USE_OQS=ON \
             -DOQS_INCLUDE_DIR=/path/to/oqs/include \
             -DOQS_LIB=/path/to/oqs/lib
    ```
    Replace `/path/to/oqs/include` and `/path/to/oqs/lib` with your actual `liboqs` installation paths.

3.  **Build the project:**
    ```bash
make
```
    This will compile the source code and generate the `chronos_node` and `wallet_cli` executables in the `build/` directory.

### Build Types

*   **Debug build:**
    ```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug
make
```
*   **Release build:** (Optimized for performance)
    ```bash
cmake .. -DCMAKE_BUILD_TYPE=Release
make
```

### Running the Node

Once built, you can run the node executable:

*   **With default configuration:**
    ```bash
./chronos_node
```
    The node will automatically load `config/default.toml`.

*   **With a custom configuration file:**
    ```bash
./chronos_node --config /path/to/your/custom_config.toml
```

## Wallet CLI

The `wallet_cli` is a command-line tool used for secure key management and validator setup. It handles cryptographic key generation, storage, and configuration with user-friendly encoding.

### Key Management Features

*   **Secure Key Storage:** Private keys are stored in encrypted files (`~/.chronos/keys/`) instead of plaintext configuration.
*   **Base58Check Encoding:** Public keys are displayed in a shorter, user-friendly Base58Check format with error detection.
*   **Key ID System:** Each key is given a user-friendly identifier (e.g., "validator-1") instead of working with long hex strings.

### Generating Validator Keys

To generate a new Dilithium key pair for a validator:

```bash
./build/wallet_cli generate-keys validator-1
```

This will:
1. Generate a new Dilithium-3 key pair (post-quantum cryptography)
2. Store the private key securely in `~/.chronos/keys/validator-1.key`
3. Display the Base58Check-encoded public key for your configuration
4. Create a validator address (Bech32 format)

### Listing Available Keys

To see all stored key IDs:

```bash
./build/wallet_cli list-keys
```

### Displaying Public Keys

To display the Base58Check-encoded public key for an existing key:

```bash
./build/wallet_cli show-public validator-1
```

### Configuration with Key Manager

After generating keys, update your `config/default.toml` with the Key ID instead of the full hex string:

```toml
# config/default.toml

[crypto]
# Use the key ID instead of pasting the full private key
# The actual key file is stored securely in ~/.chronos/keys/validator-1.key
private_key_id = "validator-1"

[consensus]
# Use the Base58Check-encoded public key (much shorter and with error detection)
validators = [
    "cqc1zg4ptpfysee0jvkgzd70pk4nef5fazw3v8y4uu"  # Replace with output from wallet_cli
]
```

**Benefits of this approach:**
*   ✅ Config file is clean and readable
*   ✅ Private keys never exposed in configuration
*   ✅ Base58Check public keys are ~60% shorter than hex
*   ✅ Error detection prevents typos in public keys
*   ✅ Multiple validators easy to manage (just create validator-2, validator-3, etc.)

### Key Security

*   Private keys are stored with restricted file permissions (owner-readable/writable only)
*   Keys are cached in memory for performance but can be cleared
*   The `~/.chronos/keys/` directory should be backed up and protected
*   Never commit key files to version control

## Project Structure

The Chronos project follows a modular structure:

*   `config/`: Configuration files for the node.
*   `proto/`: Protobuf definition files for network messages.
*   `external/`: Vendored external dependencies (e.g., BLAKE3).
*   `src/`: Core source code, organized by component:
    *   `address/`: Address generation and management.
    *   `consensus/`: Proof-of-Time (PoT) and Byzantine Fault Tolerance (BFT) logic.
    *   `crypto/`: Cryptographic primitives (hashing, signing).
    *   `ledger/`: Blockchain ledger, blocks, and transactions.
    *   `node/`: Main node application logic and configuration.
    *   `p2p/`: Peer-to-peer networking (gossip protocol, transport).
    *   `rpc/`: JSON-RPC server implementation.
    *   `storage/`: Data storage interfaces and implementations.
    *   `util/`: General utility functions (logging, byte manipulation).
*   `tests/`: Unit and integration tests for various components.
*   `wallet/`: Client-side wallet application (e.g., CLI).

For a detailed breakdown of the directory structure, please refer to `Repository.md`.

## Protobuf Integration

Chronos uses Protobuf for efficient binary serialization of P2P and BFT messages. The build system automatically compiles the `.proto` files into C++ source code during the CMake configuration step:

```cmake
find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/p2p_messages.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/bft_messages.proto
)
add_executable(chronos_node ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(chronos_node ${Protobuf_LIBRARIES})
```

If you need to manually compile `.proto` files (e.g., for development or debugging):
```bash
protoc --cpp_out=. proto/p2p_messages.proto proto/bft_messages.proto
```

## Development Conventions

*   **Coding Style:** Adhere to the existing C++ coding style. `clang-format` is recommended for automated formatting.
    ```bash
clang-format -i src/**/*.cpp src/**/*.h
```
*   **Dependencies:** Managed via CMake `FetchContent`. New dependencies should be added to `CMakeLists.txt` and documented in `Repository.md`.
*   **Modularity:** New features or components should be developed with modularity in mind, fitting into the logical structure (e.g., `src/consensus`, `src/p2p`).
*   **Configuration:** Utilize TOML files for new configurable settings.

## Post-Quantum Cryptography (PQC)

Chronos supports Dilithium signatures from `liboqs` for post-quantum security. To enable it, configure CMake with:
```bash
cmake .. -DCHRONOS_USE_OQS=ON
```

## Troubleshooting

*   **Protobuf not found:** Check your `protoc --version` and review CMake output for protobuf-related errors. Ensure it's correctly installed and discoverable by CMake.
*   **`liboqs` not found:** If `CHRONOS_USE_OQS` is enabled, ensure `liboqs` is installed and its include/library paths are correctly provided to CMake. Alternatively, set `-DCHRONOS_USE_OQS=OFF`.
*   **Build errors:** Verify your C++ compiler (GCC 10+, Clang 12+) and CMake (v3.18+) versions meet the prerequisites.

## Genesis Block and Network Initialization

The genesis hash is a critical component that ensures all nodes in the network start from the same initial state. For detailed information about how the genesis hash works and how to configure it for your network, see:

*   `GENESIS_HASH_EXPLAINED.md` (Nederlandse uitleg)
*   `GENESIS_HASH_EXPLAINED_EN.md` (English explanation)

**Quick Start:**
1. Configure `genesis.consensus_time` and `genesis.allocations` in `config/default.toml`
2. Start the first node to generate the genesis hash
3. Copy the hash to `genesis.expected_hash` in the configuration
4. Distribute the complete configuration file to all network participants

## Further Information

For more detailed information, including answers to frequently asked questions and an in-depth look at the project's internal workings, please refer to:

*   `FAQ.md`
*   `Repository.md`
*   `GENESIS_HASH_EXPLAINED.md` / `GENESIS_HASH_EXPLAINED_EN.md` - Genesis block and network synchronization