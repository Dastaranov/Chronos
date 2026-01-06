# Chronos Upgradability & Future-Proofing Strategy

This document outlines the architectural decisions and strategies to ensure Chronos can evolve over time, accommodating new cryptographic standards (e.g., Post-Quantum advancements), storage engines, and consensus improvements without requiring catastrophic rewrites.

## 1. Modular Architecture (Local Upgrades)

These upgrades affect how a node operates internally but do not change the consensus rules. A node operator can choose to use a different database or logging system without affecting the network.

### Strategy: Strict Interface Segregation
We currently use interfaces for key components. We must strictly adhere to this pattern.

*   **Storage**: `IBlockchainStorage`
    *   *Current*: LevelDB, Memory.
    *   *Future*: RocksDB, SQL, IPFS.
    *   *Implementation*: New backends are added as C++ classes implementing the interface. Selection is done via `config.toml`.
*   **Time Synchronization**: `ITimeSyncBackend`
    *   *Current*: NTP, Chrony.
    *   *Future*: Atomic Clock Driver, Quantum Link.
    *   *Implementation*: Plugin system where the `ExternalTimeSourceManager` loads the backend specified in config.

### Action Items
*   [x] **Abstract Hashing**: Currently `blake3` is used directly. We should introduce an `IHasher` interface. While changing the hash algorithm is a hard fork, having it behind an interface makes the code refactor trivial (changing one line in the factory vs 1000 lines of code).
    *   *Status*: Implemented `ICryptoProvider` and refactored `Block` class to use it.

## 2. Protocol Upgrades (Network-Wide Hard Forks)

These upgrades change the rules of the game (e.g., changing the signature scheme from Dilithium2 to Dilithium5, or changing the block hash algorithm). All nodes must upgrade simultaneously to avoid a chain split.

### Strategy: Block Versioning & Feature Flags
We will implement a mechanism similar to Bitcoin's BIP-9 (Version bits) or Ethereum's Hard Forks.

1.  **Block Header Versioning**: The `Block` struct already has a `version` field.
2.  **Activation Heights**: The code will contain logic for *both* the old and new rules.
    ```cpp
    if (current_height < FORK_HEIGHT_V2) {
        verify_signature_v1(tx);
    } else {
        verify_signature_v2(tx);
    }
    ```
3.  **Governance Signaling**: Use the existing `NodeRegistry` and Voting mechanism.
    *   Validators vote on a `PROPOSAL_UPGRADE_V2`.
    *   Once >66% approve, a `fork_height` is set in the state.
    *   Nodes automatically switch rules at that height.

### Action Items
*   [ ] **Implement Version Router**: A component that routes calls to the correct logic based on block height.
*   [ ] **Governance Upgrade Proposal**: Add a new transaction type `PROPOSAL_UPGRADE` that defines a version number and activation height.

## 3. Cryptographic Agility

Post-Quantum cryptography is evolving. Dilithium might be superseded.

### Strategy: Crypto-Suites
Instead of hardcoding "Dilithium", we define "Crypto Suites".

*   `Suite 0`: BLAKE3 + Dilithium2 (Current)
*   `Suite 1`: SHA3 + Falcon (Hypothetical)

**Implementation**:
*   Keys and Signatures are prefixed with a `SuiteID` byte.
*   The `KeyManager` and `Verifier` check this byte and dispatch to the correct library (e.g., liboqs wrapper).
*   This allows the network to support multiple algorithms simultaneously during a transition period.

## 4. Smart Execution Runtime (Long Term)

To allow changing the *logic* of the chain without recompiling the C++ binary.

### Strategy: WASM Runtime (WebAssembly)
Integrate a WASM VM (like `wasm3` or `wavm`).
*   The "State Machine" (currently C++ code in `State.cpp`) becomes a WASM binary stored on-chain.
*   To upgrade the chain logic, a transaction uploads a new WASM binary.
*   All nodes execute the new binary for subsequent blocks.

This is the "Holy Grail" of upgradability (used by Polkadot/Substrate), but requires significant engineering effort.

## 5. Data Migration Strategy

If we switch from LevelDB to RocksDB, or change the data format.

### Strategy: Snapshot-based Migration
Instead of writing complex migration scripts:
1.  Node exports state to a portable Snapshot format (JSON/Binary).
2.  Node shuts down.
3.  Node restarts with new binary/config.
4.  Node imports state from Snapshot into the new storage engine.

## Summary of Immediate Next Steps
1.  **Refactor Hashing**: Wrap `blake3` calls in a `CryptoProvider` class.
2.  **Enhance Governance**: Add `UPGRADE` proposal type.
3.  **Version Handlers**: Ensure `Block` and `Transaction` validation logic checks the `version` field.
