# Chronos Blockchain - Frequently Asked Questions

## Table of Contents

1. [General Information](#general-information)
2. [Installation & Setup](#installation--setup)
3. [Network & Communication](#network--communication)
4. [Cryptography & Security](#cryptography--security)
5. [Consensus & Validation](#consensus--validation)
6. [Ledger & State Management](#ledger--state-management)
7. [Performance & Resources](#performance--resources)
8. [Development & API](#development--api)
9. [Roadmap & Future Development](#roadmap--future-development)

---

## General Information

### What is Chronos?

Chronos is a modern C++20 blockchain node implementation featuring Byzantine Fault Tolerant (BFT) consensus, Proof-of-Time (PoT) aggregation, and post-quantum cryptography support. The project emphasizes modularity, security, and future-readiness with support for Dilithium signatures via the liboqs library.

### What are the key features?

- **Modern C++20 Architecture**: Leverages contemporary C++ standards for performance and safety
- **Modular Design**: Separated concerns across consensus, P2P networking, storage, and cryptography
- **Post-Quantum Ready**: Optional Dilithium signature support for quantum-resistant security
- **BFT Consensus**: Byzantine Fault Tolerant consensus mechanism for network agreement
- **Proof-of-Time**: Time aggregation using NTP measurements with statistical outlier filtering
- **Protobuf Messaging**: Efficient binary serialization for P2P communication
- **TOML Configuration**: Human-readable configuration files for easy setup

### What is the current development status?

Chronos is in active development. Core functionality is implemented but several features are still being enhanced. See the [Roadmap & Future Development](#roadmap--future-development) section for details on planned improvements.

---

## Installation & Setup

### What are the system requirements?

**Minimum Requirements:**
- C++20 compatible compiler (GCC 10+ or Clang 12+)
- CMake 3.18 or higher
- Protocol Buffers (libprotoc) 3.21.12 or higher
- 2GB RAM for light nodes, 4GB+ for full nodes
- 10GB+ disk space for full nodes

**Recommended:**
- Modern multi-core processor
- 8GB+ RAM for full node operation
- SSD storage for improved I/O performance
- Stable internet connection with at least 1 Mbps bandwidth

### How do I build and run Chronos?

```bash
# Clone the repository
git clone https://github.com/yourusername/Chronos
cd Chronos

# Create build directory
mkdir build && cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build
make

# Run with default configuration
./chronos_node

# Or run with custom configuration
./chronos_node --config /path/to/config.toml
```

### How do I enable post-quantum cryptography?

To enable Dilithium support via liboqs:

```bash
cmake .. -DCHRONOS_USE_OQS=ON \
         -DOQS_INCLUDE_DIR=/path/to/oqs/include \
         -DOQS_LIB=/path/to/oqs/lib
make
```

Ensure you have liboqs installed and provide the correct paths to its include directory and library.

### What is the difference between full nodes and light nodes?

**Full Nodes:**
- Store complete blockchain history
- Validate all blocks and transactions independently
- Can serve blockchain data to light nodes
- Require significant storage and memory resources
- Configured with `node_type = "full"` in config file

**Light Nodes:**
- Store only block headers and minimal state
- Rely on full nodes for transaction validation
- Reduced resource requirements
- Suitable for resource-constrained environments
- Configured with `node_type = "light"` in config file

---

## Network & Communication

### How do nodes communicate with each other?

Nodes communicate via a peer-to-peer gossip protocol running over TCP/IP. The `SocketTransport` class manages direct connections, and messages are serialized using Google Protocol Buffers for efficiency. Messages are defined in `.proto` files and include blocks, transactions, and BFT consensus messages.

### How does peer discovery work?

Chronos uses a seed peer mechanism for network bootstrapping:

1. New nodes connect to initial seed peers listed in the configuration file (`config/default.toml`)
2. Seed peers respond with a list of other known peers (`PeerListMessage`)
3. The node progressively discovers the network through these connections
4. No central server or authority is required for peer discovery

This decentralized approach ensures network resilience and censorship resistance.

### How do I configure network settings?

Edit the `[network]` section in your configuration file:

```toml
[network]
listen_addr = "0.0.0.0"        # Listen on all interfaces
listen_port = 8645              # P2P port
seeds = ["127.0.0.1:8645"]     # Initial seed peers
```

For production deployment, add multiple reliable seed peers to ensure successful network bootstrap.

### Will firewalls and NAT cause problems?

Nodes behind strict firewalls or NAT that block incoming connections cannot function as fully participating peers. They can initiate outbound connections but cannot receive incoming connections, which limits network decentralization.

**Recommended Solutions:**
- **Port Forwarding**: Forward the P2P port (default 8645) in your router to the machine running the node
- **DMZ Configuration**: Place the node in the router's DMZ (less secure but effective)
- **VPS Deployment**: Run nodes on cloud servers with public IP addresses

Future enhancements may include UPnP (Universal Plug and Play) or STUN/TURN server support for automatic NAT traversal.

### What are the default ports?

- **P2P Communication**: Port 8645 (TCP)
- **RPC Server**: Port 8080 (HTTP)

These can be configured in the `[network]` and `[rpc]` sections of the configuration file.

---

## Cryptography & Security

### What cryptographic algorithms does Chronos use?

- **Hashing**: BLAKE3 for block hashing and address generation
- **Standard Signatures**: Configurable signing algorithms
- **Post-Quantum Signatures**: Dilithium (via liboqs, optional)
- **Address Encoding**: Bech32m with configurable HRP (default "cqc")
- **Address Format**: BLAKE3 hash of public key truncated to 160 bits (20 bytes)

### How are private keys managed?

Chronos implements a secure key management system via the `wallet_cli` tool:

- **Secure Storage**: Private keys are stored in `~/.chronos/keys/` with owner-only file permissions (600)
- **Key IDs**: Keys are referenced by user-friendly identifiers (e.g., "validator-1")
- **Never in Config**: Private keys are NEVER stored in configuration files
- **Base58Check Encoding**: Public keys are displayed in Base58Check format for brevity and error detection

### How do I generate and manage keys?

```bash
# Generate a new key pair
./wallet_cli generate-keys validator-1

# List all stored keys
./wallet_cli list-keys

# Display public key for configuration
./wallet_cli show-public validator-1
```

The displayed public key can then be added to the `validators` list in your configuration file.

### Does Chronos support address rotation?

Currently, Chronos does not implement automatic address rotation (HD wallet functionality). Each address is a direct hash of a public key. While users can manually generate and use multiple key pairs, there is no built-in mechanism for automatic address management. This functionality would need to be implemented in an external wallet application.

---

## Consensus & Validation

### How does the BFT consensus work?

Chronos implements a Byzantine Fault Tolerant consensus mechanism:

1. **Round-based**: Consensus progresses through numbered rounds at each block height
2. **Leader Selection**: A designated leader proposes blocks for each round
3. **Voting Phases**: Validators vote in two phases (Prevote and Precommit)
4. **Quorum Requirement**: 2/3+ majority required for block finalization
5. **Safety Guarantee**: Ensures consistency even with up to 1/3 malicious validators

The consensus state machine is implemented in the `BftGadget` class.

### What is Proof-of-Time (PoT)?

Proof-of-Time is a time synchronization mechanism that:

- Aggregates time measurements from multiple NTP servers
- Uses median and MAD (Median Absolute Deviation) statistics
- Filters outliers based on configurable `outlier_mad_factor`
- Provides consensus time for block timestamps
- Ensures temporal consistency across the network

This prevents time-based attacks and ensures fair consensus timing.

### How are validators configured?

Validators are configured in the `[consensus]` section of the configuration file:

```toml
[consensus]
validators = [
    "base58check_encoded_public_key_1",
    "base58check_encoded_public_key_2",
    # ...
]
```

All nodes in the network must have identical validator sets for consensus to function correctly.

### What validation is performed on transactions?

Current transaction validation includes:

- **Signature Verification**: Cryptographic validation of transaction signatures
- **Basic Format Checks**: Validation of required fields and data structures
- **Nonce Verification**: Ensuring correct transaction sequence (planned)
- **Balance Validation**: Checking sufficient funds for transaction amount plus fees (planned)
- **Duplicate Detection**: Preventing replay attacks (planned)

Enhanced validation features are currently under development.

### Can I query transaction status?

Currently, transaction status queries are limited. You can:

- Check if a transaction is in the mempool via RPC
- Verify if a transaction was included in a finalized block
- Query account balance changes resulting from transactions

More detailed transaction tracking and history features are planned for future releases.

---

## Ledger & State Management

### How do I query an address balance?

Use the JSON-RPC interface (default port 8080):

```json
{
    "jsonrpc": "2.0",
    "method": "get_balance",
    "params": {
        "address": "cqc1q..."
    },
    "id": 1
}
```

The response will contain the current balance for the specified address.

### How is state stored?

Chronos uses a modular storage approach:

- **State Storage**: Account balances and nonces stored via `IKv` interface
  - `FileKv`: Text-based hexadecimal format (current default)
  - Future: Binary format for efficiency
- **Blockchain Storage**: Implements `IBlockchainStorage` interface
  - `DiskBlockchainStorage`: Persistent storage for full nodes
  - `MemoryBlockchainStorage`: In-memory storage for light nodes and testing

### Can I query historical address balances?

The current implementation maintains only the current state. Historical balance queries are not supported. The ledger tracks the latest balance and nonce for each address but does not maintain a complete transaction history per address.

This functionality could be added through:
- Transaction indexing by address
- Periodic state snapshots
- Archive node functionality

### What is the snapshot system?

Snapshots provide point-in-time backups of blockchain state:

- **Purpose**: Fast node synchronization and state recovery
- **Contents**: Complete account state (balances and nonces)
- **Format**: JSON-based (current), binary format planned
- **Status**: Core framework implemented, full serialization in development

Snapshots enable new nodes to sync quickly without replaying the entire blockchain history.

---

## Performance & Resources

### What is the expected CPU usage?

CPU usage varies significantly based on network activity:

**Idle State:**
- Minimal CPU usage (< 1%)
- Node primarily waits for network messages

**Active State (peak usage):**
- **Cryptographic Operations**: Signature verification (Dilithium is compute-intensive)
- **Block Validation**: Processing transactions and state updates
- **Consensus Participation**: BFT voting and message handling
- **Hashing**: BLAKE3 operations for blocks and transactions

**Estimate**: Low to moderate average CPU usage with periodic spikes during consensus rounds and block validation.

### What is the expected memory usage?

**Full Node:**
- **State Data**: Scales linearly with number of unique addresses
- **Estimate**: 500MB to several GB depending on network size
- **Primary Factor**: In-memory account state map

**Light Node:**
- **Block Headers**: Minimal storage
- **Estimate**: 100-500MB
- **Note**: Current `MemoryBlockchainStorage` is not production-ready for large chains

Memory usage grows with network adoption and transaction activity.

### What is the expected disk usage?

**Full Node:**
- **Current Storage**: Text-based hexadecimal format (inefficient)
- **Overhead**: 2x or more compared to binary storage
- **Growth Rate**: Rapid growth with blockchain activity
- **Estimate**: Several GB to hundreds of GB depending on chain length

**Light Node:**
- **Minimal**: Only configuration and log files
- **Estimate**: < 1GB

**Future Optimization**: Migration to binary storage format (LevelDB planned) will significantly reduce disk usage.

### How can I optimize performance?

**Hardware Recommendations:**
- Use SSD storage instead of HDD for faster I/O
- Allocate adequate RAM (8GB+ for full nodes)
- Multi-core processor for parallel signature verification

**Configuration Tuning:**
- Adjust `bft_round_timeout_ms` for network conditions
- Configure appropriate `outlier_mad_factor` for time synchronization
- Optimize `gossip_topics` to reduce message overhead

**Network Optimization:**
- Ensure low-latency connection to seed peers
- Use geographically distributed seed peers
- Consider dedicated server deployment for validators

---

## Development & API

### What RPC methods are available?

Current JSON-RPC methods include:

- `get_balance`: Query account balance by address
- `get_status`: Retrieve node status and network information
- `send_transaction`: Submit a transaction to the network (planned)
- Additional methods under development

RPC handlers are implemented in `src/rpc/handlers.cpp`.

### How do I interact with the node programmatically?

The JSON-RPC interface provides programmatic access:

```bash
# Using curl
curl -X POST http://localhost:8080/rpc \
  -H "Content-Type: application/json" \
  -d '{
    "jsonrpc": "2.0",
    "method": "get_balance",
    "params": {"address": "cqc1q..."},
    "id": 1
  }'
```

Libraries supporting JSON-RPC 2.0 can be used from any programming language.

### Can I build a custom monitoring dashboard?

Yes, the foundation is already implemented:

- **Node Status**: `NodeStatus` struct contains metrics like block height, mempool size, and peer count
- **Console Display**: `ConsoleDisplay` class provides terminal-based rendering
- **RPC Access**: `get_status` method exposes status information

To extend monitoring:
1. Add desired metrics to `NodeStatus` struct
2. Update `ConsoleDisplay` rendering logic
3. Expose metrics via RPC for external dashboards

### What logging capabilities are available?

Chronos implements a comprehensive logging system:

**Features:**
- **Categories**: GENERAL, WALLET, CONSENSUS, P2P, STATE, CRYPTO, LEDGER, STORAGE
- **Levels**: INFO, WARN, ERROR, DEBUG
- **Outputs**: Console and file (`chronos_log_<date>.txt`)
- **Usage**: `LOG_INFO(LogCategory::CONSENSUS, "Message with {}", arg)`

**Configuration:**
- Initialize with `LOG_INIT(".")` in main
- Adjust verbosity as needed
- Separate console display from log spam

### How can I contribute to development?

Chronos welcomes contributions:

1. Review the project structure and coding conventions
2. Check `TODO.md` for planned features and known issues
3. Study the modular architecture (consensus, P2P, storage, crypto)
4. Submit pull requests with clear descriptions
5. Follow existing code documentation standards
6. Write tests for new functionality

See the project repository for contribution guidelines.

---

## Roadmap & Future Development

### What features are currently in development?

**High Priority:**
- **Thread Safety**: Mutex protection for mempool, peer management, and blockchain state
- **Block Finalization**: Complete BFT precommit quorum logic
- **Transaction Validation**: Enhanced mempool validation with balance and nonce checking
- **Signature Verification**: Full integration of signature verification in consensus messages
- **State Serialization**: Binary format for efficient snapshots

**Medium Priority:**
- **Canonical Serialization**: Fixed-width integers and proper endianness for wire format
- **LevelDB Storage**: Efficient blockchain storage backend
- **Enhanced RPC**: Additional API methods and transaction submission
- **Network Improvements**: UPnP support, better peer scoring

### What are known limitations?

**Current Limitations:**
- Incomplete transaction validation in mempool
- Snapshot serialization is placeholder-based
- Text-based storage format (inefficient)
- No automatic NAT traversal
- Limited RPC functionality
- No HD wallet support
- No historical state queries

See `TODO.md` for comprehensive tracking of limitations and planned improvements.

### Will the protocol change in the future?

Protocol evolution is expected and necessary:

**Backward Compatibility:**
- Protobuf schema changes must maintain compatibility where possible
- Breaking changes require coordinated network upgrades (hard forks)
- Version negotiation in handshake protocol

**Planned Changes:**
- Migration to canonical binary serialization
- Enhanced BFT message validation
- Improved time synchronization (NTS support)
- Storage layer optimizations

Network upgrades will be coordinated through governance mechanisms as the network matures.

### What is the long-term vision?

Chronos aims to be:

- **Secure**: Post-quantum cryptography and robust consensus
- **Scalable**: Efficient storage and state management
- **Decentralized**: Accessible to operators with diverse resources
- **Developer-Friendly**: Clear APIs and comprehensive documentation
- **Future-Ready**: Adaptable architecture for emerging requirements

The project continues to evolve based on community needs and technological advances.

---

## Additional Resources

- **Documentation**: See `README.md` for build instructions and feature overview
- **Development Guide**: Check `.github/copilot-instructions.md` for architecture details
- **Issue Tracking**: `TODO.md` lists known issues and planned features
- **Staging Folders**: Historical development stages in `staging/` directory provide architectural insights

For questions not covered in this FAQ, please consult the source code documentation or reach out to the development community.
