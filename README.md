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
*   **Advanced Time Synchronization:** Integrates with Chrony/NTS and supports high-precision time sources (Atomic/Quantum clocks) with a tiered quality system.

## Time Synchronization

Chronos relies on precise timekeeping for its Proof-of-Time (PoT) consensus. It implements a sophisticated multi-tiered time synchronization system:

### Time Tiers
Nodes are categorized into 5 tiers based on their time source accuracy:
*   **Tier 1 (Quantum):** Quantum Clock Backend (Highest precision).
*   **Tier 2 (Atomic):** Atomic Clock Backend.
*   **Tier 3 (GNSS/GPS):** Direct GNSS/GPS hardware.
*   **Tier 4 (Chrony/NTS):** Authenticated Network Time Security (Required for Validators).
*   **Tier 5 (NTP):** Standard NTP (Fallback, not suitable for validation).

### Time Quality Score (0-100)
Each node calculates a `TimeQualityScore` based on:
*   **Source Precision:** The inherent accuracy of the backend.
*   **Stability:** Variance in measurements over time.
*   **Drift:** Clock drift rate relative to the network median.

Validators must maintain a minimum **Tier 4** status. Nodes falling below this tier are demoted and cannot participate in block validation until their time source recovers.

### Chrony Integration
For Tier 4 compliance, Chronos integrates with **Chrony** using the Network Time Security (NTS) protocol. This ensures authenticated and encrypted time synchronization, preventing Man-in-the-Middle (MitM) attacks on time data.

## Building and Running

### Prerequisites

Ensure you have the following installed:

*   **C++20 Compatible Compiler:** GCC 10+ or Clang 12+.
*   **CMake:** Version 3.18 or higher.
*   **Protobuf:** `libprotoc` version 3.21.12 or higher.
*   **LevelDB:** `libleveldb-dev` (for high-performance storage).
*   **Snappy:** `libsnappy-dev` (compression for LevelDB).
*   **Chrony:** `chrony` (for secure time synchronization).
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
    This will compile the source code and generate the `chronos_node`, `wallet_cli`, `node_cli`, and `genesis_tool` executables in the `build/` directory.

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

### Network Connectivity
*   **Dynamic IPs:** Regular nodes work fine with dynamic IPs.
*   **Validators:** A **Static IP** is strongly recommended for validators to ensure consistent uptime and rewards. See `USER_MANUAL.md` for details.

## Node Dashboard
The Chronos node features a real-time console dashboard that displays:
- **Node Status**: ID, ports, storage usage.
- **Blockchain Metrics**: Current height, consensus round, mempool size.
- **Validator Status**: Number of active validators and pending governance votes.
- **Logs & Activity**: Live stream of node logs and blockchain events.

## Wallet CLI

The `wallet_cli` is a command-line tool used for secure key management and validator setup. It handles cryptographic key generation, storage, and configuration with user-friendly encoding.

### Key Management Features

*   **Secure Key Storage:** Private keys are stored in encrypted files (`~/.chronos/keys/`) instead of plaintext configuration.
*   **Base58Check Encoding:** Public keys are displayed in a shorter, user-friendly Base58Check format with error detection.
*   **Key ID System:** Each key is given a user-friendly identifier (e.g., "validator-1") instead of working with long hex strings.
*   **Multi-Node Support:** Connect to multiple nodes with automatic failover and discovery.

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

### Connecting to Nodes

The wallet can connect to remote nodes. Configuration is stored in `~/.chronos/wallet_config.json`.

```bash
# Add a node to the configuration
./build/wallet_cli --rpc 192.168.1.10:8080 list-nodes

# Discover more nodes from connected peers
./build/wallet_cli discover

# List configured nodes
./build/wallet_cli list-nodes
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

## Governance & Staking
Chronos implements a Proof-of-Authority / Proof-of-Stake hybrid model:
- **Registration**: Nodes register via `STAKE_REGISTRATION` transactions.
- **Approval Flow**: New nodes must be voted in by existing validators using `VOTE` transactions. A candidate requires >50% approval from the active validator set to join.
- **Slashing**: Validators can be slashed (stake burned, suspended) for misbehavior (e.g., double signing, low uptime).
- **Dynamic Validator Set**: The active validator set updates automatically based on stake and approval status.

### Remote Usage (Wallet & Node CLI)
Both `wallet_cli` and `node_cli` can connect to a remote node instead of a local one. This allows users to manage wallets or administer nodes from a different machine.

```bash
# Connect to a remote node
./build/wallet_cli balance cqc1... --rpc 192.168.1.50:8080

# Use with API key (if node requires it)
./build/node_cli status --rpc node.chronos-network.io:443 --api-key "your-secret-key"
```

## Security Features

*   **Post-Quantum Cryptography:**
    *   **Signatures:** Dilithium (ML-DSA) for transaction and block signing.
    *   **Key Encapsulation:** Kyber (ML-KEM) for P2P transport encryption.
*   **Time Security:**
    *   **Tiered Verification:** Validators enforced to Tier 4+ (Authenticated NTS).
    *   **Anti-Spoofing:** Statistical filtering of time samples (Median/MAD).
    *   **Time Quality Score:** Dynamic scoring of node time reliability.
*   **Transport Security:** AES-256-GCM encryption for all P2P communication.
*   **Secure Key Storage:** Encrypted key files with strict permissions.
*   **Sybil Resistance:**
    *   **Staking:** Minimum stake required to validate.
    *   **IP Limiting:** Limits connections per IP address to prevent flooding.
    *   **System Lock:** Prevents multiple node instances on the same machine.
*   **BFT Consensus:** Byzantine Fault Tolerant consensus with 2/3 quorum.

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

## 🛠️ CLI Tools

Chronos provides two CLI tools:
1. **wallet_cli**: For general users to manage keys and send transactions.
2. **node_cli**: For node operators to manage staking, voting, and node status.

### Wallet CLI (User)
```bash
# Generate a new key pair
./build/wallet_cli generate-keys my-wallet

# Check balance
./build/wallet_cli balance <address>

# Send tokens
./build/wallet_cli send my-wallet <recipient_address> <amount_nanos>
```

### Node CLI (Admin)
```bash
# Check node status
./build/node_cli status

# List connected peers
./build/node_cli peers

# View mempool transactions
./build/node_cli mempool

# Stake to become a validator
./build/node_cli stake my-validator-key 1000000000

# Vote for a candidate
./build/node_cli vote my-validator-key <candidate_address> approve

# List candidates
./build/node_cli list-candidates
```

### 💰 Token Generation (Faucet)
For the testnet, the genesis block allocates initial funds to a specific "faucet" address.
To get tokens:
1. Import the genesis private key (if you are the admin).
2. Or ask the network administrator to send tokens to your address.
3. Future versions will include a public faucet API.

## 🔐 Security Features

*   **LevelDB/Snappy errors:** Ensure you have installed the development libraries:
    ```bash
    sudo apt install libleveldb-dev libsnappy-dev
    ```
*   **Protobuf not found:** Check your `protoc --version` and review CMake output for protobuf-related errors. Ensure it's correctly installed and discoverable by CMake.
*   **`liboqs` not found:** If `CHRONOS_USE_OQS` is enabled, ensure `liboqs` is installed and its include/library paths are correctly provided to CMake. Alternatively, set `-DCHRONOS_USE_OQS=OFF`.
*   **Build errors:** Verify your C++ compiler (GCC 10+, Clang 12+) and CMake (v3.18+) versions meet the prerequisites.

## Further Information

For more detailed information, including answers to frequently asked questions and an in-depth look at the project's internal workings, please refer to:

*   `FAQ.md`
*   `Repository.md`