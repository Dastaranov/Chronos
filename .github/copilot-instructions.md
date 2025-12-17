# Chronos Blockchain - AI Agent Instructions

## Project Overview
Chronos is a C++20 blockchain node implementing BFT consensus and Proof-of-Time (PoT) aggregation with post-quantum cryptography (Dilithium via liboqs). The architecture is modular with clear separation between consensus, P2P, storage, and crypto layers.

## Staging Folders - Historical Context
The `staging/` directory contains historical development stages (_Chronos_Stage1 through Stage6). **These folders should be consulted when developing new features** to understand:
- Evolution of component architecture (e.g., how BFT state machine evolved)
- Previously attempted patterns and why they changed
- Lessons learned from earlier iterations (threading issues, serialization approaches)
- Missing pieces that were planned but not yet implemented

Use staging folders as a reference to avoid repeating past mistakes and to discover forgotten design decisions.

## Architecture Essentials

### Core Components
- **NodeApp** (`src/node/node_app.{hpp,cpp}`): Main orchestrator integrating all subsystems. Runs event loop managing P2P messages, consensus rounds, snapshots, and peer discovery.
- **BftGadget** (`src/consensus/bft.{hpp,cpp}`): Byzantine Fault Tolerant consensus. Tracks rounds/heights, handles Prevote/Precommit/NewRound messages, manages validator sets.
- **PoTAggregator** (`src/consensus/pot_aggregator.{hpp,cpp}`): Proof-of-Time using median/MAD statistics on NTP measurements. Filters outliers with `outlier_mad_factor`.
- **Gossip** (`src/p2p/gossip.{hpp,cpp}`): P2P message distribution using `SocketTransport`. Handles block/transaction/BFT message propagation.
- **State** (`src/ledger/state.{hpp,cpp}`): Account balances and nonces. Uses `IKv` for persistence (typically `FileKv`).

### Data Flow
1. **Transactions**: Arrive via RPC → validated → added to mempool → proposed in blocks by BFT leader
2. **Consensus**: Leader proposes block → validators Prevote → Precommit on 2/3+ quorum → block finalized
3. **Time Sync**: ExternalTimeSourceManager queries NTP servers → PoTAggregator filters/aggregates → `consensus_time` embedded in blocks
4. **Storage**: Blocks saved via `IBlockchainStorage` (DiskBlockchainStorage for full nodes, MemoryBlockchainStorage for light nodes)

### Message Protocol
- **P2P Messages**: Protobuf-defined (`proto/p2p_messages.proto`). Contains `HandshakeMessage`, `BlockMessage`, `TransactionMessage`, and embedded BFT messages.
- **BFT Messages**: Separate protobuf (`proto/bft_messages.proto`): `Prevote`, `Precommit`, `NewRound` with signatures.
- **Serialization**: Blocks/Transactions use custom binary serialization (`serialize()`/`deserialize()` methods). JSON used only for snapshots/config.

## Build System & Dependencies

### CMake Structure
- **FetchContent Dependencies**: nlohmann/json, toml++, cpp-httplib fetched automatically
- **Vendored Dependencies**: BLAKE3 in `external/blake3/`
- **Optional liboqs**: Enable with `-DCHRONOS_USE_OQS=ON` (default ON). Paths hardcoded in CMakeLists.txt to `/home/gert/Dev/oqs_install/`
- **Protobuf**: System-installed (≥3.21.12). Proto files compiled during CMake configure, output to `src/proto/*.pb.{h,cc}`

### Build Commands
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug  # or Release
make
./chronos_node  # Uses config/default.toml by default
./chronos_node --config path/to/custom.toml
```

### Testing
- Test framework in `tests/test_framework.{hpp,cpp}` with custom macros: `TEST_CASE()`, `ASSERT_TRUE()`, `ASSERT_EQ()`, etc.
- Run individual test binaries from `build/` directory
- No unified test runner - each test file compiles to separate executable
- **Testing is performed manually** - no CI/CD automation currently in place

## Key Management System

### Secure Storage Pattern
**CRITICAL**: Private keys never stored in TOML config. Use KeyManager (`src/crypto/key_manager.{hpp,cpp}`):
- Keys stored in `~/.chronos/keys/` with owner-only permissions
- Reference by key_id in config: `private_key_id = "validator-1"`
- Public keys displayed in Base58Check format (60% shorter than hex, with checksum)

### Wallet CLI Workflow
```bash
# Generate and store key
./build/wallet_cli generate-keys validator-1

# List available keys
./build/wallet_cli list-keys

# Show public key for config
./build/wallet_cli show-public validator-1  # Returns Base58Check-encoded pubkey
```

Update `config/default.toml`:
```toml
[crypto]
private_key_id = "validator-1"  # NOT the raw hex key

[consensus]
validators = ["cqc1zg4ptpfysee0..."]  # Base58Check pubkey from wallet_cli
```

## Coding Conventions

### Code Documentation Standards
**CRITICAL**: All code must be thoroughly documented with clear, descriptive comments.

**Required Documentation:**
- **File Headers**: Every .cpp/.hpp file must have a header comment explaining its purpose
- **Class Documentation**: Each class needs a clear description of its role and responsibilities
- **Method Documentation**: All public and protected methods must have:
  - Brief description of what the method does
  - @param tags explaining each parameter
  - @return tag describing return value (if applicable)
  - @throws tag for exceptions that may be thrown
- **Complex Logic**: Any non-trivial algorithm or logic block must have explanatory comments
- **TODOs**: Clearly marked with context: `// TODO: [Description of what needs to be done]`
- **Implementation Notes**: Use inline comments to explain "why" not just "what"

**Documentation Style:**
- Use Doxygen-compatible comment blocks (`/**` for documentation, `//` for inline)
- Keep comments up-to-date with code changes
- Write comments in clear, concise English
- Avoid redundant comments that merely repeat the code
- Focus on explaining intent, edge cases, and non-obvious decisions

**Example:**
```cpp
/**
 * @brief Validates and processes an incoming Prevote message.
 * 
 * Performs comprehensive validation including validator authentication,
 * height/round matching, block hash validation, and duplicate detection.
 * Upon reaching quorum, locks the block and transitions to precommit phase.
 *
 * @param prevote The Prevote message received from a validator.
 * @return An optional Precommit message if quorum is reached and block is locked,
 *         nullopt otherwise.
 */
std::optional<chronos::bft::Precommit> handle_prevote(const chronos::bft::Prevote& prevote);
```

### Logging
Use centralized logging system (`src/util/log.hpp`):
- Initialize with `LOG_INIT(".")` in main
- Categories: GENERAL, WALLET, CONSENSUS, P2P, STATE, CRYPTO, LEDGER, STORAGE
- Levels: INFO, WARN, ERROR, DEBUG
- Pattern: `LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Message with {}", arg);`

### Memory Management
- Smart pointers preferred: `std::unique_ptr` for ownership, `std::shared_ptr` sparingly
- Forward declarations extensively used to reduce compilation dependencies
- RAII pattern for resource cleanup (sockets, file handles, threads)

### Namespaces
- `chrono_node`: Node application logic
- `chrono_consensus`: BFT and PoT
- `chrono_p2p`: Networking
- `chrono_ledger`: Blocks, transactions, state
- `chrono_crypto`: Signing, hashing
- `chrono_storage`: Persistence
- `chrono_util`: Utilities (logging, bytes, console)
- `chrono_address`: Address generation/validation

### Address Format
- Bech32m encoding with configurable HRP (default "cqc")
- Address = BLAKE3(public_key) truncated to 20 bytes
- Compile definition: `CHRONOS_BLAKE3_ADDR_160=1`

### Error Handling
- Exceptions for initialization failures and unrecoverable errors
- Return empty/invalid states for recoverable failures (e.g., `Address::is_valid()`)
- Log errors before throwing with appropriate category

## Configuration System

### TOML Structure
See `config/default.toml` for canonical structure:
- `[node]`: `node_type` (full/light), `data_dir`
- `[network]`: `listen_addr`, `listen_port`, `seeds`
- `[rpc]`: `port`
- `[consensus]`: `bft_round_timeout_ms`, `validators`, `outlier_mad_factor`, etc.
- `[crypto]`: `private_key_id` (references KeyManager), `sign_alg`, `addr_hrp`
- `[external_time_sources]`: `ntp_servers`, `ntp_query_interval_ms`

### Loading Pattern
```cpp
chrono_node::Config cfg = chrono_node::Config::load(config_path);
```
Config merges with defaults. Missing values throw exceptions.

## Common Development Tasks

### Adding New Consensus Messages
1. Define protobuf in `proto/bft_messages.proto`
2. Rebuild to generate C++ bindings
3. Add handler in `NodeApp::handle_p2p_message()`
4. Update `BftGadget` state machine if needed
5. Add gossip topic if broadcasting required

### Adding Storage Backends
1. Implement `IBlockchainStorage` interface (`src/storage/IBlockchainStorage.hpp`)
2. Add to NodeApp constructor based on `cfg.node_type`
3. Test with both DiskBlockchainStorage and MemoryBlockchainStorage patterns

### Debugging Tips
- Logs written to both console and `./chronos_log_<date>.txt`
- Use `ConsoleDisplay` class for real-time status updates without log spam
- BFT state tracked via `bft_height_`, `bft_round_` in NodeApp
- Check `TODO.md` for known issues and planned improvements
- Detailed Implementation Steps

### 1. Thread Safety (HIGHEST PRIORITY)
Current state: Only `PeerManager::peers_` and `FileKv` operations are mutex-protected. Critical race conditions exist:

**Missing Mutexes:**
- `NodeApp::mempool_` (`std::vector<Transaction>`) - accessed from RPC thread and consensus loop
  - Add: `std::mutex mempool_mutex_` to NodeApp class
  - Protect: `add_transaction_to_mempool()`, `get_mempool_const()`, and consensus block creation
- `NodeApp::connected_peers_` (`std::unordered_map<string, PeerInfo>`) - modified during handshakes and peer management
  - Add: `std::mutex peers_mutex_` to NodeApp class  
  - Protect: `connected_peers_` access in `handle_p2p_message()`, `update_peer_score()`, `manage_peers()`
- `NodeApp::last_block_hash_` and `next_block_height_` - read/written without synchronization
  - Add: `std::mutex blockchain_state_mutex_` to NodeApp class
  - Protect: All reads/writes of these fields

**Implementation Steps:**
1. Add mutex members to `src/node/node_app.hpp` private section
2. Wrap all accesses with `std::lock_guard<std::mutex>` 
3. Review `src/node/node_app.cpp` for concurrent access patterns (RPC handlers, P2P callbacks, main loop)
4. Test under load with multiple simultaneous RPC requests and P2P messages

### 2. Block Finalization Logic
**Current Gap**: `BftGadget` counts Precommits but NodeApp doesn't trigger finalization

**Implementation Steps:**
1. Add `BftGadget::check_precommit_quorum(height, round)` method returning bool
2. In `NodeApp::run()` main loop, after processing messages:
   ```cpp
   if (bft_->check_precommit_quorum(next_block_height_, current_round)) {
       finalize_block_at_height(next_block_height_);
   }
   ``` (handled by separate agent - do not modify)
3. Implement `NodeApp::finalize_block_at_height()`:
   - Lock `blockchain_state_mutex_`
   - Persist block via `blockchain_storage_->saveBlock()`
   - Update `last_block_hash_` and `next_block_height_`
   - Clear mempool of included transactions
   - Advance BFT to next height
4. Add logging with `LOG_INFO(CONSENSUS, "Finalized block at height {}", height)`

### 3. State Serialization (Snapshots)
**Current Gap**: `snapshots.cpp` uses placeholder JSON, no real state persistence

**Implementation Steps:**
1. Design binary format for State serialization:
   - Magic bytes (4 bytes): "CSST" (Chronos State Snapshot)
   - Version (uint32_t LE)
   - Account count (varint)
   - For each account: address (20 bytes) + balance (uint64_t LE) + nonce (uint64_t LE)
2. Implement `State::serialize_to_bytes()` in `src/ledger/state.cpp`
3. Implement `State::deserialize_from_bytes(const Bytes&)` with validation
4. Update `SnapshotManager::create_snapshot()` to call `state_.serialize_to_bytes()`
5. Update `NodeApp::start_from_snapshot()` to restore full state
6. Add mutex protection for State operations during snapshot creation

### 4. Signature Verification in Consensus
**Current Gap**: BFT messages have signature field but verification not called

**Implementation Steps:**
1. In `NodeApp::handle_p2p_message()`, when receiving Prevote/Precommit/NewRound:
   ```cpp
   // Extract validator public key from cfg_.validators list
   // Call signer_->verify(public_key, message_hash, signature)
   if (!verified) {
       update_peer_score(sender_id, -10, true);
       return; // Drop message
   }
   ```
2. Implement message hash computation for BFT messages (serialize all fields except signature)
3. Add validator lookup by `validator_id` in messages
4. Log verification failures with `LOG_WARN(CONSENSUS, "Invalid signature from {}", validator_id)`

### 5. Transaction Validation
**Current Gap**: `add_transaction_to_mempool()` has basic checks only

**Implementation Steps:**
1. Add to `NodeApp::add_transaction_to_mempool()`:
   - Check transaction not already in mempool (compare tx hash)
   - Verify sender has sufficient balance: `state_.get_balance(tx.from) >= tx.amount + tx.fee`
   - Verify nonce matches expected: `state_.get_nonce(tx.from) == tx.nonce`
   - Verify signature with `signer_->verify(tx.from_pubkey, tx.get_hash(), tx.signature)`
2. Return descriptive errors for each failure case
3. Add duplicate detection using `std::unordered_set<Bytes>` of tx hashes in mempool
4. Log rejected transactions with specific reason

### 6. Canonical Serialization
**Current Gap**: Uses `size_t` and native endianness in Block/Transaction serialize()

**Implementation Steps:**
1. Create `src/util/codec.hpp` with:
   - `write_varint(vector<uint8_t>&, uint64_t)` 
   - `read_varint(const uint8_t*&, size_t&) -> uint64_t`
   - `write_fixed_uint64_le()`, `write_fixed_uint32_le()` etc.
2. Update `Block::serialize()`:
   - Replace all `size_t` with explicit `uint64_t` or `uint32_t`
   - Use LE helpers for all multi-byte integers
   - Use varint for counts/lengths
3. Update `Transaction::serialize()` similarly
4. Bump protocol version in handshake to indicate format change
5. Add migration notes in `TODO.md` for wire format breaking change

### Storage Roadmap (Planned)
- Implement LevelDB-backed `IBlockchainStorage` (alongside Disk/Memory)
- Use Protobuf for block serialization on disk (reuse p2p wire types) to avoid duplicate codecs
- Append-only layout: write blocks sequentially without full rewrites
- Indexing: `h/<uint64 height>` → block hash; `b/<block_hash>` → block bytes; `m/meta` → chain tip/meta
- Integrity: maintain Merkle-root per segment; store checksum/length per record; fast `getBlockByHeight` via height index
- Compaction/GC: define compaction policy; ensure snapshot safety during compaction

### LevelDB Storage API Sketch
- `bool save_block(const Block& blk)`; writes protobuf-serialized block bytes and updates `h/` + tip meta atomically
- `std::optional<Block> get_block_by_hash(const Bytes& hash)`; reads `b/<hash>`
- `std::optional<Block> get_block_by_height(uint64_t h)`; resolves `h/<height>` → hash → block
- `std::optional<BlockMeta> get_tip()` / `bool set_tip(const BlockMeta&)`; stores height+hash in `m/meta`
- `bool has_block(const Bytes& hash)`; existence check without full read
- `AppendResult append_blocks(span<Block>)`; batch append for catchup (fsync boundaries configurable)
- Integrity hooks: `validate_record_checksum`, `verify_segment_merkle`

### LevelDB Storage - Additional Details
- Key schema: `h/<uint64 height>` (LE-encoded, zero-padded) → block hash; `b/<block_hash>` → protobuf bytes; `m/meta` → tip meta struct (height + hash)
- Atomicity: `save_block` must update block bytes, height index, and tip meta in one batch write
- Append-only discipline: never rewrite existing block records; only append new `b/` and `h/` entries and update `m/meta`
- Integrity fields: store length + checksum (e.g., CRC32c) alongside each `b/` record; maintain Merkle root per segment (e.g., every N blocks) for fast verification
- Fsync strategy: allow config for fsync frequency (every write vs. every N blocks) to trade durability vs. throughput
- Compaction policy: avoid compacting active segment; ensure snapshot safety by pausing snapshot reads or pinning SSTs during snapshot creation
- Error handling: surface typed errors (NotFound, Corrupt, IoError); `save_block` returns false on partial write or checksum mismatch; `append_blocks` reports first failing height/hash
- Recovery path: on startup, validate `m/meta` against `h/` and `b/` entries; rebuild tip if meta is behind; drop incomplete tail if checksum or Merkle validation fails
- **Canonical Serialization**: Endianness and fixed-width types needed for wire format

## Testing Patterns
```cpp
TEST_CASE("TestName") {
    // Setup
    chrono_ledger::Block block;
    
    // Execute
    auto hash = block.get_header_hash();
    
    // Verify
    ASSERT_EQ(hash.size(), 32);
    ASSERT_TRUE(block.is_valid());
}
```
- Use `chrono_util::hex_to_bytes()` / `bytes_to_hex()` for test data
- Mock IKv/IBlockchainStorage with MemoryKv/MemoryBlockchainStorage
- Tests auto-register via `TEST_CASE()` macro

## External Dependencies Notes

## SecureSync Timeserver (Planned)
- Phase 1: software timeserver with Chrony; support NTP and NTS (TLS + AEAD)
- Trusted NTS servers: time.cloudflare.com, nts.netnod.se, ptbtim1.ptb.de, 1.ntp.ubuntu.com (extendable)

### API Sketch
- `struct TimeSample { std::chrono::milliseconds offset; double rtt_ms; std::string source; bool authenticated; };`
- `class ITimeSyncBackend { virtual std::optional<TimeSample> query(const std::string& server) = 0; };`
- `class ChronyBackend : public ITimeSyncBackend { /* uses Chrony/NTS */ };`
- `class SecureTimeServer { bool start(); void stop(); TimeSample get_last_best(); };`
- `class ExternalTimeSourceManager` updates PoT via callback; swap backend to `ChronyBackend` when ready.

### Integration Notes
- Config: add NTS server list, TLS/NTS enable flag, max skew, query interval; default list includes the trusted servers above.
- Wiring: `ExternalTimeSourceManager` should accept an `ITimeSyncBackend` and emit `TimeMeasurement` objects to `PoTAggregator`.
- Auth: mark samples `authenticated=true` when NTS succeeds; drop/penalize unauthenticated or high-skew samples.
- Validation: reject samples beyond configured skew; require minimum NTS quorum before using for consensus time.
- Health/logging: per-server stats (success/fail, rtt, offset), and fallbacks if NTS fails (fail fast, retry with backoff).

### Migration Plan (SecureSync)
1) Config: add `[secure_time]` section with `nts_servers`, `enable_nts`, `max_skew_ms`, `query_interval_ms`; keep legacy `external_time_sources` as fallback.
2) Backend: implement `ChronyBackend` (ITimeSyncBackend) and inject into `ExternalTimeSourceManager` via constructor/setter; keep current backend as default when disabled.
3) Data flow: `ExternalTimeSourceManager` emits `TimeMeasurement` to `PoTAggregator`; extend measurement to carry `authenticated` flag and source.
4) Validation: in manager, discard samples over `max_skew_ms`; require NTS quorum before promoting authenticated time to consensus_time.
5) Logging/metrics: per-server stats, last authenticated sample, and failure counters; expose via existing logging + optional RPC hook.
6) Rollout: feature-flag via config; if Chrony/NTS unavailable, auto-fallback to current NTP flow and log downgrade.
## When Working With...
- **Addresses**: Always validate with `Address::is_valid()` before use
- **Blocks**: Call `calculate_merkle_root()` before `get_header_hash()`
- **Configs**: Changes to default.toml should be documented in README.md
- **Protobuf**: Re-run CMake after modifying .proto files
- **Signers**: Use ISigner interface, not concrete types (DilithiumSigner, HmacSigner)
