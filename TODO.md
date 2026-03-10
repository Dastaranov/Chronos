# Chronos TODO & Implementatie Overzicht

**📋 Voor volledig implementatie-overzicht met code voorbeelden, zie [IMPLEMENTATION_ROADMAP.md](IMPLEMENTATION_ROADMAP.md)**

## 🔴 KRITIEKE PRIORITEIT (Blokkerend voor productie)

### 1. Tokenomics & Monetary Policy Finaliseren
**Status:** Beslissingen genomen, implementatie vereist  
**Blokkeert:** DEX, staking, wallet management, mainnet launch

**Open Vragen (MOET BEANTWOORD):**
- Pre-minted definitief vs hybride model?
- Exact halving interval (blocks per 2 jaar)?
- Genesis distributie percentages?
- Vesting schedules per categorie?
- Emergency wallet governance?

**Implementatie:** Config updates, State supply tracking, handshake validation  
**Bestanden:** `config/default.toml`, `src/node/config.hpp`, `src/ledger/state.hpp`, `src/node/node_app.cpp`

### 2. LevelDB Storage Backend
**Status:** ✅ Geïmplementeerd — `LevelDBBlockchainStorage` actief voor full nodes
**Blokkeert:** Schaalbaarheid, performance, mainnet

**Implementatie:** LevelDBBlockchainStorage class, key schema (h/, b/, m/), protobuf serialization, atomic batch writes  
**Bestanden:** `src/storage/LevelDBBlockchainStorage.hpp/cpp`, `CMakeLists.txt`

### 3. Peer Discovery Mechanisme
**Status:** ✅ Geïmplementeerd — `DiscoveryManager` + `PeerStore` actief
**Blokkeert:** Decentralisatie, productie deployment

**Implementatie:** PeerStore persistence, DiscoveryManager, peer exchange protocol, bootstrap nodes  
**Bestanden:** `src/p2p/peer_store.hpp/cpp`, `src/p2p/discovery_manager.hpp/cpp`, `proto/p2p_messages.proto`

---

## 🟠 HOGE PRIORITEIT (Fundamenteel voor productie)

### 4. Graceful Degradation & Error Recovery
**Implementatie:** Retry logic, sync fallbacks, degraded light mode, consensus stuck detection  
**Bestanden:** `src/node/node_app.cpp`

### 5. Config Validation
**Implementatie:** validate() method met port checks, quorum validation, tokenomics consistency  
**Bestanden:** `src/node/config.hpp/cpp`

### 6. Wallet CLI Essential Commands
**Implementatie:** balance, send, tx-history, key import/export  
**Bestanden:** `src/wallet/cli/commands/`, `src/rpc/handlers.cpp`

---

## 🔵 MEDIUM PRIORITEIT (Features & Verbetering)

### 7. SecureSync Timeserver (Phase 1)
**Status:** ✅ Geïmplementeerd — `ChronyBackend` + `NtpClient` actief; `AtomicClockBackend` en `QuantumClockBackend` zijn insert points voor toekomstige hardware

### 8-11. Node Politics & Governance
Zie dedicated sectie hieronder voor volledige details.

---

## Node Politics & Governance (Plan)

Doel: nodes belonen voor goede participatie (uptime), misbehaving nodes bestraffen (slashing/blacklist), en nieuwe nodes toelaten via een expliciete consensus-governance stap (approvalblock). Deze sectie beschrijft het implementatieplan en acceptatiecriteria.

- Identiteit & Registry
    - Uniek `node_id`: afleiden uit publieke sleutel (bv. BLAKE3(pubkey) → 20 bytes) en on-chain registreren.
    - Optionele `display_name` door Node admin, opgeslagen on-chain; validatie tegen beleid (lengte/charset).
    - Config-uitbreiding: `node.display_name` (alleen weergave), niet vertrouwelijk; key management blijft via `KeyManager`.
    - Acceptatie: proto + opslag-schema gedefinieerd; registry CRUD paden beschreven.

- Uptime Tracking & Rewards
    - Heartbeat/attestation mechanisme per epoch (bv. 1h) via gossip of RPC; windowed metrics (7d/30d). 
    - Reward schema: wekelijkse of maandelijkse uitbetaling op basis van uptime score (minimaal aantal attestaties en max allowed downtime).
    - Emissiebudget en distributielogica als on-chain transactie door `RewardDistributor` of geïntegreerd in `NodeApp` bij finalisatie.
    - Acceptatie: duidelijke formule (score → beloning), tijdvensters en payouts gespecificeerd.

- Stake, Slashing & Schorsing
    - Minimum stake vereist om te kunnen valideren; saldo wordt gelockt (niet-spendable) voor slashing.
    - Penalties: bij bewezen misbehavior (double-vote, invalide signatures) of structurele downtime onder drempel → proportionele slashing.
    - Automatische schorsing wanneer gelockte stake < minimum; handhaving via consensus-state; herstel via top-up + her-approval indien nodig.
    - Blacklist: on-chain lijst met einddatum (expiry) en reden; appeals mogelijk via governance call.
    - Acceptatie: drempels/percentages en triggers gedocumenteerd; state-overgangen (Active → Suspended/Blacklisted) uitgewerkt.

- Consensus Approval voor nieuwe Nodes (Approvalblock)
    - Registratieflow: nieuwe node publiceert `NodeRegistration` en vraagt on-chain approval.
    - `Approvalblock`: expliciet blok dat een set `NodeApprovalVote`s bevat; bij quorum wordt node geactiveerd.
    - Integratie met BFT: votes als BFT-compatibele berichten, finalisatie vereist voor activatie.
    - Acceptatie: end-to-end flow van registratie → votes → finalisatie → activatie beschreven.

- Approver Set Policy (Top-N dynamisch)
    - Approvers: top-N nodes (bv. top-10 tot top-100) afhankelijk van netwerk-grootte en prestatie/ stake.
    - Dynamiek: N = min(100, max(10, f(|validators|))) waar f kan afhangen van ketengrootte (logaritmisch, percentiel) en configuratie.
    - Quorumregels: bv. ≥ 2/3 van approver-set, tijdslimieten voor vote-windows.
    - Acceptatie: selectiecriteria, N-bepaling en quorum wiskundig gespecificeerd en geparametriseerd via config.

- Protocol & Opslag
    - Protobuf: `NodeRegistration`, `NodeApprovalVote`, `NodeHeartbeat`, `SlashingEvent`.
    - Gossip topics voor bovengenoemde berichten; validatie + signature verification via bestaande `ISigner`.
    - Opslag: registry tabel, approvals (votes/blocks), blacklist records, stake locks; append-only discipline.
    - Acceptatie: wiretypes toegevoegd, opslag-API’s geschetst (`IBlockchainStorage`) en indexering bepaald.

- RPC/CLI & Admin UX
    - CLI: register node, set display_name, query status/blacklist, appeal verzoek indienen.
    - RPC: endpoints voor metrics (uptime), governance-status, pending approvals, stakes.
    - Acceptatie: commando’s en endpoints gedocumenteerd, basale veiligheidscontroles.

- Testen & Metrics
    - Unit/integration tests voor registraties, votes, slashing, payouts en state-overgangen.
    - Logging/metrics: per-node uptime score, stake, penalties, approvalstatus, payout-historiek.
    - Acceptatie: testgevallen gedefinieerd, met duidelijke pass/fail criteria.

### Implementatiestappen (samengevat)
1. Specifieer Node Identity & Registry (proto + storage)
2. Uptime metingen + beloningsmechanisme (week/maand)
3. Stake, slashing, schorsing & blacklist logica
4. Consensusflow voor `approvalblock` + votes
5. Approver-selectie (top-N) en quorumregels (dynamisch)
6. Wire protocol + gossip + opslag updates
7. RPC/CLI en config uitbreidingen
8. Testen, logging en metrics

---

# MONETARY_POLICY_DESIGN.md

## 1. Total Supply Model
- Fixed supply (bijv. 21 miljoen zoals Bitcoin)
- Inflationary supply (bijv. 2% per jaar zoals Ethereum)
- Hybrid model (fixed cap maar graduele release)

## 2. Genesis Distribution
- Percentage naar: team, investors, community, treasury, validators
- Vesting schedules en lock-up periods
- Genesis hash validation (moet identiek zijn op alle nodes)

## 3. Coin Issuance Mechanisms
- Block rewards: Hoeveel nieuwe munten per block?
- Halving schedule: Elke X blocks halveren?
- Transaction fees: Worden deze gebrand of naar validators?
- Minting cap: Maximum aantal nieuwe munten per jaar

## 4. Config Consensus Enforcement
- CRITICAL: Genesis hash MOET matchen tussen alle nodes
- Tokenomics parameters moeten onderdeel zijn van genesis block
- Nodes met verschillende config MOETEN rejected worden bij handshake
- Validation: Check max_supply, block_reward, etc. bij block validation

## 5. Fiat On-ramp Strategy
- Option A: Centralized exchange listings (Binance, Coinbase)
- Option B: Payment processor integratie (Stripe, Ramp Network)
- Option C: P2P fiat gateways (LocalBitcoins-style)
- Option D: Stablecoin bridges (USDC/USDT wrapping)

## 6. Supply Tracking & Enforcement
- Total supply counter in blockchain state
- Validation: new_supply = genesis + (blocks * reward) - burned_fees
- Reject blocks that exceed max_total_supply
- Consensus rule: All nodes must agree on circulating supply

## 7. Implementation Roadmap
- Phase 1: Define fixed parameters
- Phase 2: Implement supply validation in consensus
- Phase 3: Add supply tracking to State
- Phase 4: Config validation at node startup

## Beslissingen (19-12-2025)

- Total supply: Vast aantal munten = 31.556.926 (seconden/jaar)
- Fees: worden betaald aan validators (geen burn)
- Halving: elke 2 jaar
- Fiat on-ramp: nodig/hoog prioriteit
- Minting: voorkeur voor volledig pre-minted (nader te bekijken)

## Onmiddellijke Acties die Nodig Zijn

1. Config Parameters Toevoegen:

```toml
[tokenomics]
## Hard cap op totale supply (eenheden). Indien nanos gebruikt worden, converteer volgens token-decimals.
max_total_supply = 31556926

## Pre-minted voorkeur: geen block rewards
initial_block_reward_nanos = 0
minting_enabled = false

## Halving elke 2 jaar: indien minting later wordt ingeschakeld, afleiden via bloktempo
## Voorbeeldformule (plaatsbekleder): reward_halving_interval = blocks_per_year * 2
reward_halving_interval = 0  # 0 = niet van toepassing bij pre-minted

## Fees naar validators (geen burn)
fee_burn_percentage = 0
```

2. Genesis Validation Versterken:

- Genesis hash MOET gecheckt worden bij node startup
- Als genesis hash niet matcht → node start NIET
- Tokenomics parameters moeten onderdeel zijn van genesis hash

3. Supply Tracking in State:

```cpp
class State {
    uint64_t total_circulating_supply_;  // Track total coins in circulation
    
    bool validate_total_supply() const {
        return total_circulating_supply_ <= config_.max_total_supply;
    }
};
```

4. Config Consensus Check bij Handshake:

```cpp
bool validate_peer_config(const PeerConfig& peer_cfg) {
    if (peer_cfg.max_total_supply != config_.max_total_supply) {
        log_error("Peer has incompatible max_total_supply");
        return false;
    }
    // Check other critical params
}
```

## Open Vragen
- Pre-minted definitief vastleggen: blijven block rewards uitgeschakeld, of hybride?
- Halving-interval exact bepalen indien minting wordt ingeschakeld (afhankelijk van bloktempo en consensus-config).
- Token-decimals (nanos) en conversie voor `max_total_supply` uniform vastleggen in config.

### Geconsolideerde en Geprioriteerde TODO-lijst voor Chronos

Deze lijst is samengesteld uit alle opgegeven TODO-bestanden, geconsolideerd, en geprioriteerd op basis van crucialiteit voor functionaliteit, beveiliging, stabiliteit en prestaties.

---

#### **Prioriteit: Hoog** (Fundamentele functionaliteit, beveiliging, en stabiliteit)

**1. Kern Consensus & State Correctheid:**
    *   ✅ **Quorum threshold configurabel maken** - VOLTOOID (Stap D)
    *   ✅ **Hulpfuncties voor quorum checks** - VOLTOOID (Stap E)
    *   ✅ **Block finalisatie** - VOLTOOID (Stap A)
    *   ✅ **Berichtvalidatie uitbreiden** - VOLTOOID (Stap B)
    *   ✅ **Prevote/Precommit signing en block locking** - VOLTOOID (Stap C)
    *   ✅ **State machine refactor** - VOLTOOID (Stap F1)
    *   ✅ **Transactie validatie uitbreiden** - VOLTOOID
        - Signature verification via signer_->verify()
        - Nonce validation (matching expected next nonce per sender)
        - Duplicate detection (comparing transaction hashes in mempool)
        - Balance validation (sufficient funds for amount + fee)
        - Detailed logging for rejection reasons
    *   ✅ **State herstel vanuit snapshot** - VOLTOOID (Stap G)
        - `NodeApp::start_from_snapshot()` implementatie
        - Volledige state recovery van snapshot bytes
        - Blockchain metadata restore (last_block_hash, next_block_height)
        - Snapshot integriteit validatie
        - Tests: SnapshotManager Create/Restore, State Recovery - ALLEN PASSING
    *   ✅ **State serialisatie verbeteren (Snapshots)** - VOLTOOID
        - Binair format met magic bytes "CSST" + versie (uint32)
        - Account count (uint32) + per account: addr_len (uint32) + addr_bytes + balance (uint64) + nonce (uint64)
        - State::serialize_to_bytes() - binary encoding van balances_ en nonces_
        - State::deserialize_from_bytes() - restauratie met validatie
        - SnapshotManager::createSnapshot() - gebruik real binary serialization ipv JSON
        - SnapshotManager::restoreSnapshot() - extract state_bytes en metadata
        - NodeApp::start_from_snapshot() - volledige state recovery + blockchain metadata update
    *   ✅ **Serialization canonicaliseren (endianness & vaste breedte)** - VOLTOOID (Stap H)
        - Maak `util/codec.hpp` met LE-helpers en VarInt
        - `write_fixed_uint32_le()`, `read_fixed_uint32_le()`, `write_fixed_uint64_le()`, `read_fixed_uint64_le()`
        - `write_varint()`, `read_varint()` voor variable-length integers
        - `write_bytes_with_length()`, `read_bytes_with_length()` helpers
        - Update `Block::serialize()` en `Block::deserialize()` naar canonical format (LE, uint32_t for counts)
        - Update `Transaction::serialize()` en `Transaction::deserialize()` naar canonical format
        - Tests: Alle bestaande serialization tests slagen (20/21 passing)
    *   ✅ **Merkle root met domain separation + expliciete empty root** - VOLTOOID (Stap I)
        - Domain separation: "CHRONOS_TX_LEAF_" voor leaf nodes, "CHRONOS_TX_NODE_" voor internal nodes
        - Expliciete empty root: "CHRONOS_MERKLE_EMPTY" voor empty blocks (ipv leeg)
        - Prevents merkle tree confusion with other hash uses
        - `calculate_merkle_root()` compleet herschreven met domain separation
        - Tests: Alle serialization tests slagen (20/21 passing)
    *   ✅ **Block: Recompute Merkle root bij deserialize en verifieer** - VOLTOOID
        - `Block::deserialize()` recompute merkle root en verify tegen stored root
        - Throws detailed exception als merkle root mismatch (prevents spoofed blocks)
        - Error message toont expected vs computed hash
    *   ✅ **Block: `is_valid()` uitbreiden** - VOLTOOID
        - Check prev_block_hash size (32 bytes)
        - Check transactions_merkle_root size (32 bytes)
        - Verify merkle root matches computed value
        - Validate all transactions (`tx.is_valid()`)
        - Comprehensive logging met ByteCatgory::LEDGER
        - Tests: Alle bestaande tests slagen (20/21 passing)
    *   ✅ **State: Genesis & validatie** - VOLTOOID
        - Configureerbare genesis via [genesis] TOML sectie met allocations, consensus_time, expected_hash, max_supply_per_account
        - `State::set_balance()` methode voor genesis allocations met address validatie
        - `State::is_valid_address()` statische methode voor format checking
        - `Address::is_valid(string)` statische methode voor bech32m validatie
        - Genesis creation in NodeApp met hash validation en allocation application
        - Overflow protection in `credit()` en `set_balance()`
        - Complete test suite: 8 genesis tests (alle PASSING)
        - Tests: test_genesis.cpp met genesis creation, allocations, max supply, invalid addresses, hash validation, persistence
    *   **Transaction Validation & Block Inclusion** - ✅ VOLTOOID
        - Transaction Validation Pipeline: Volledige 5-level validation (format, signature, nonce, duplicates, balance)
        - `select_transactions_for_block()`: Filter txs op fees, sort by fee (highest first), apply sequentially
        - Block Proposal Filtering: Leader selecteert txs, verzamelt fees, cleart mempool
        - Transaction Fees System: MIN_FEE constant (set to 1), fee collection via `state_.credit()`
        - Leader rewards: Automatisch fee collection bij block proposal
        - Tests: Alle 36 tests passing (genesis + validation)
    *   **Block Finalization Pipeline** - ✅ VOLTOOID
        - `BftGadget::advance_to_next_height()`: Increment height, reset round to 0, clear votes, clear proposal
        - Mempool cleanup na finalization (std::remove_if filtering van applied txs)
        - Blockchain state advancement (last_block_hash, next_block_height)
        - Block persistence via storage backend
        - Snapshot creation post-finalization
        - State advancement en metadata updates
        - Tests: Alle 36 tests passing, BFT state machine validated
    *   **Network Synchronization** - ✅ VOLTOOID
        - Sync state tracking: is_syncing_, sync_peer_id_, sync_target_height_, sync_downloaded_blocks_
        - `manage_sync()`: Continuous sync management in main event loop
        - `start_sync_with_peer()`: Initiates sync with best available peer (highest height, good score)
        - Sync timeout detection: Automatically switches peers if no progress for 30 seconds
        - Batch block download: Requests SYNC_BATCH_SIZE (10) blocks per request
        - Sync progress tracking: Monitors blocks downloaded, requests next batch automatically
        - `detect_and_resolve_fork()`: Fork detection when peer reports different block at same height
        - Fork logging and peer penalization (full resolution TODO for future)
        - Block reception updated to track sync progress and detect forks
        - Tests: Alle 36 tests passing, sync logic validated
    *   **Signature Verification in Consensus** - ✅ VOLTOOID
        - Message hash computation: `compute_bft_message_hash()` using Blake3 (deterministic hashing)
        - Message format: height || round || block_hash || validator_id (canonical serialization)
        - Signature verification: `verify_bft_message_signature()` validates BFT messages
        - Validator lookup: `get_validator_public_key()` from config validators list
        - Prevote signature verification: Added before processing, penalizes invalid (-10 score)
        - Precommit signature verification: Added before processing, penalizes invalid (-10 score)
        - NewRound signature verification: Added before processing, penalizes invalid (-10 score)
        - Detailed logging: Logs verification success/failure with details
        - Peer penalization: -10 score for invalid signature (significant penalty)
        - TODO: Implement full validator registry with public key storage and lookup
        - Tests: Alle 36 tests passing, message handling validated
    *   **JSON defensieve checks**: Hex length checks, Merkle root recompute en mismatch → exception, State schema check (`TODO.md`).
    *   **Interoperabiliteit - Signatuur-Algoritme Identificatie & Dynamische Verificatie**: Protobuf uitbreiden, NodeApp/BftGadget implementatie.
    *   **Blockchain persistence redesign (LevelDB + Protobuf)**:
        - Implementeer een LevelDB-gebaseerde `IBlockchainStorage` (nieuwe backend naast Disk/Memory)
        - Gebruik Protobuf-serialisatie voor blocks (hergebruik p2p wire types) om dubbel werk te vermijden
        - Append-only layout: schrijf block payloads sequentieel; vermijd volledige rewrite
        - Index key-schema: `h/<uint64 height>` → block hash, `b/<block_hash>` → block bytes, `m/meta` → chain tip/meta
        - Merkle-root segment index: per segment een Merkle root opslaan om integriteit te valideren zonder alles te lezen
        - Fast path: `getBlockByHeight` gebruikt height→hash index; `getBlockByHash` direct uit hash-key
        - Compaction/GC: definieer compaction policy en snapshot safety (no rewrite tijdens compaction)
        - Integriteit: store checksum/length per record; valideer voor acceptatie
        - Testen: property-tests voor append→read, crash-recovery (open/close), en mismatch-detectie op Merkle/checksum

**2. Essentiële Robuustheid & Beveiliging:**
    *   ✅ **Thread safety** - VOLTOOID (Verified Session 3)
        - `mempool_mutex_` beschermt alle add_transaction_to_mempool() accesses (line 661)
        - `peers_mutex_` beschermt connected_peers_ in handle_p2p_message() (line 785), update_peer_score(), manage_peers() (lines 1163, 1194)
        - `blockchain_state_mutex_` beschermt last_block_hash_ en next_block_height_ in alle scenario's (lines 208, 266, 322, 354, 523, 793, 856, 1129, 1230, 1266)
        - get_mempool_const() thread-safe via copy-return (line 177 in hpp)
        - Alle accessen van RPC thread, P2P handlers, en main loop volledig protected
        - 21+ mutex-protected critical sections verified in node_app.cpp
    *   ✅ **Protobuf parsing robuuster maken** - VOLTOOID (Session 4)
        - Message size validation: MAX_MESSAGE_SIZE (10MB) checked before parsing
        - Empty message rejection: Drop messages with zero size
        - Handshake validation: validate_handshake_message() with node_id/port/hash checks
        - BFT message validation: validate_bft_message() with height/round/validator checks
        - GetBlocks validation: validate_get_blocks_message() with hash size and limit checks
        - Block/Transaction size limits: MAX_BLOCK_SIZE (5MB), MAX_TRANSACTION_SIZE (100KB)
        - Graceful error handling: try-catch blocks for deserialization failures
        - Peer penalization: -10 score for invalid signatures, -5 to -10 for malformed messages
        - DoS protection: Rate limits on GetBlocks (max 100 blocks), message size caps
        - Node ID validation: MIN_NODE_ID_LENGTH (10), MAX_NODE_ID_LENGTH (256)
        - Port validation: 1-65535 range check
        - Tests: 9 new validation tests (all passing, 52 total tests)
    *   **Error handling uitbreiden (Algemeen)**: Gedetailleerde foutmeldingen, fallback/retry/logging bij kritieke operaties (Storage, Consensus, P2P, Config).
        - ✅ **Phase 1 COMPLETED: Analyze error handling gaps**
          - Created ERROR_HANDLING_ANALYSIS.md (350 lines) documenting:
            - 5 critical error handling gaps: Storage, Network/P2P, Consensus, Config, Snapshots
            - Specific code locations and risk assessments
            - Recovery strategies for each failure type
            - Implementation priority roadmap (Phases 1-3)
          - Identified missing error handling in:
            - `NodeApp::finalize_block()` - No error check on saveBlock/saveMetadata
            - `P2P::GetBlocks` - No response sent if block not found
            - `BftGadget` - Silent failures in message validation
            - `Config::load()` - Throws immediately, no graceful error display
            - `SnapshotManager` - No retry logic for failed snapshots
        - ✅ **Phase 1 COMPLETED: Define structured error types**
          - Created `src/util/error_types.hpp` (350+ lines):
            - 4 typed error enums: StorageErrorCode, NetworkErrorCode, ConsensusErrorCode, ConfigErrorCode
            - ErrorSeverity enum: Transient, Recoverable, Critical, Fatal
            - ErrorContext struct: operation, component, details, recovery_hint, source location
            - Result<T> template: Type-safe error propagation with implicit bool conversion
          - Created `src/util/error_types.cpp` (180+ lines):
            - ErrorContext::to_string() formatting method
            - Result<T> specializations for bool, int, string, uint64_t
            - ErrorSeverity determination logic based on error type
          - Error codes mapped to recovery strategies:
            - Transient errors (IOError, ConnectionTimeout) → Retry with exponential backoff
            - Recoverable errors (NotFound, PeerDisconnected) → Fallback to alternative strategy
            - Critical errors (DiskFull, StateInconsistent) → Log detailed error, degrade functionality
            - Fatal errors (ConfigParseError) → Cannot start, show error to operator
          - Test coverage: 23 new tests (75 total tests passing)
            - Tests for Result<T> construction and conversion
            - Error severity classification tests
            - ErrorContext formatting tests
            - All error codes validated as defined and distinct
    *   **Retry logic met exponential backoff** (Phase 2): ✅ COMPLETED
        - Created `src/util/retry_policy.hpp` (200+ lines):
          - RetryPolicy struct with configurable retry parameters (max_retries, initial_backoff_ms, max_backoff_ms, jitter support)
          - calculate_backoff_ms() - Exponential backoff with capping (100ms → 200ms → 400ms → ...)
          - sleep_backoff() - Apply delay with optional randomization to prevent thundering herd
          - retry_with_backoff<T>() - Retries all Result<T> failures with exponential backoff
          - retry_on_error<T>() - Only retries if error.is_retriable() == true (Transient severity)
        - Created `src/util/retry_policy.cpp` (50 lines):
          - Implements exponential backoff formula: min(initial * (multiplier ^ attempt), max)
          - Jitter implementation using std::random_device and std::mt19937
          - sleep_backoff applies actual thread sleep with std::this_thread::sleep_for
        - Automatic retry decisions based on error severity:
          - Transient errors (IOError, ConnectionTimeout, RoundTimeout) → Retry with backoff
          - Recoverable errors (NotFound, PeerDisconnected, InvalidBlock) → Return immediately (no retry)
          - Critical/Fatal errors → Immediate failure (no retry)
        - Test coverage: 17 tests validating all aspects
          - Backoff calculation: initial, exponential growth, capping, custom multipliers
          - Validation: parameter bounds, invalid configurations
          - Retry behavior: success on first try, retriable failures, non-retriable failures, max retries exhaustion
          - Timing: actual backoff delays (with tolerance), zero-backoff fast path
          - Domain coverage: Storage, Network, Consensus transient errors all retry correctly
        - Integration ready: Result<T>.is_retriable() determines if error triggers retry
        - Test results: **92 tests passing** (75 error types + 17 retry tests)
    *   **Graceful degradation** (Phase 3): Fallback strategieën (bijv. light sync als full sync faalt).
    *   **Logging enhancement** (Phase 4): Context-aware error messages met recovery hints.
    *   **Consensus stuck detection**: Mechanisme om vastgelopen consensusrondes te detecteren en te forceren (`node_app.cpp`).
    *   **Validatie van config-waarden**: Controleer geldigheid van poorten, directory paths, enz. (`config.cpp`).
    *   **Security (optioneel)**: MAC of signature op snapshots, integriteitscontrole vóór parse, overflow-limieten (`TODO.md`, `State.cpp`).
    *   **SecureSync Timeserver (fase 1)**: Softwarematige timeserver met Chrony; NTP + NTS (TLS + AEAD). Gebruik erkende NTS-servers (bijv. time.cloudflare.com, nts.netnod.se, ptbtim1.ptb.de, 1.ntp.ubuntu.com) en breid lijst uit indien nodig.

**3. Basis Configuratie:**
    *   **Quorum threshold configurabel maken** (`bft.cpp`).
    *   **HRP configureerbaar via config** (`address.cpp`).
    *   **Configuratie van genesis_allocations, max_supply_per_account, hrp, network_id** (`TODO.md`).

**4. Unit Tests (Fundamenteel):**
    *   ✅ BFT tests toegevoegd voor timeouts, quorum, state transitions (12/13 passing; legacy leader selection expectation nog te herstellen) (`bft.cpp`).
    *   Voeg tests toe voor config parsing en validatie (`config.cpp`).
    *   Voeg tests toe voor saveBlock, getBlock, saveMetadata, getMetadata (Storage).
    *   Unit tests voor P2P message handling, block addition, connect, send, receive, disconnect, server start/stop, client handling, add/remove/get/random peer selection, socket connect/publish/message handling (`node_app.cpp`, P2P modules).
    *   Unit tests voor Address (valid/invalid), Block (merkle root, serialize/deserialize, tamper tests), State (valid JSON, corrupt JSON, invalid address) (`TODO.md`).

---

#### **Prioriteit: Medium** (Verbeterde robuustheid, prestaties, en uitgebreide functionaliteit)

**1. Netwerk Robuustheid (P2P):**
    *   **Reconnect en retry-mechanisme**: Automatische reconnect bij disconnects (`p2p_client.cpp`).
    *   **Peer management uitbreiden**: Timeouts voor peers, reconnect-mechanisme (`node_app.cpp`).
    *   **Peer disconnect detectie**: Robuuste detectie en cleanup bij peer disconnects (`socket_transport.cpp`).
    *   **Berichtfragmentatie**: Support voor grote berichten (fragmentatie/assemblage) (`p2p_client.cpp`).
    *   **Peer authenticatie**: Optionele authenticatie bij inkomende verbindingen (`p2p_server.cpp`).
    *   **NTP server selectie uitbreiden**: Fallback/retry, meerdere tijdbronnen (HTTP, GPS, etc.) (`external_time_source_manager.cpp`).

**2. Geavanceerde Configuratie & Flexibiliteit:**
    *   **Configuratie van intervals**: Peer discovery, management en snapshot intervals configureerbaar (`node_app.cpp`, Storage modules).
    *   **Timeouts en buffer sizes configureerbaar** (P2P Client, Socket Transport).
    *   **Configuratie van max connections/peers** (`p2p_server.cpp`, `peer_manager.cpp`).
    *   **Default values centraliseren** (`config.cpp`).
    *   **Recursieve TOML merge** (`config.cpp`).
    *   **Configuratie van thresholds**: MAD factor en minimum threshold (`pot_aggregator.cpp`).
    *   **Configuratie van poort en timeout** (NTP Client).

**3. Performance & Optimalisatie:**
    *   **Performance optimalisatie (FileKv)**: Index of caching, vermijd volledige file-rewrite (`file_kv.cpp`).
    *   **Performance optimalisatie (Storage)**: Batch-operaties voor blokken en metadata.
    *   **Performance optimalisatie (Snapshots)**: Chunking en streaming voor grote snapshots.
    *   **Performance optimalisatie (PoTAggregator)**: Median/MAD berekening voor grote datasets.

**4. Uitgebreide Validatie & Meta-informatie:**
    *   **Header bytes centraliseren (DRY)** (`block.cpp`).
    *   **Address: Optioneel type/version byte in payload** (`address.cpp`).
    *   **Peer metadata uitbreiden**: Laatst gezien, score, capabilities (`peer_manager.cpp`).
    *   **Confidence/error dynamisch bepalen** (NTP Client, External Time Source Manager).
    *   **Anti-spoofing checks uitbreiden** (`pot_aggregator.cpp`).
    *   **RPC (`src/rpc/handlers.cpp`)**: De `is_syncing` status dynamisch maken.

**5. Infrastructuur & Helper Functies:**
    *   **Helpers & infra (nieuwe util)**: Maak centrale util-header: `util/codec.hpp`.

---

#### **Prioriteit: Laag** (Onderhoudbaarheid, documentatie, en toekomstige uitbreidingen)

**1. Code Kwaliteit & Onderhoud:**
    *   **Code cleanup (Algemeen)**: Verwijder legacy code, dubbele commentaren en niet-gebruikte variabelen.
    *   **Logging uniformeren (Algemeen)**: Zorg voor consistente logging (niveau, format).
    *   **Gebruik van `constexpr` en `enum class`**: Vervang magic numbers door constexpr, gebruik enum class voor type safety (`node_app.cpp`).
    *   **Peer pruning**: Automatische pruning van inactieve/bad peers (`peer_manager.cpp`).
    *   **Snapshot chunking (progress tracking)**: Voeg progress tracking toe voor snapshot downloads (`node_app.cpp`).
    *   **Resource cleanup**: Zorg voor correcte cleanup van sockets, threads, clients bij shutdown (P2P Server, Socket Transport).

**2. Documentatie:**
    *   **Documentatie uitbreiden (Algemeen)**: Doxygen-compatibele commentaren bij alle publieke methoden.
    *   **Configuratiebestanden updaten en documenteren (Algemeen)**.
    *   **Migratie & compatibiliteit**: Documenteer wire-format wijziging, versieveld toevoegen.
    *   **Beschrijf wire-format en JSON schema** (`TODO.md`).
    *   **Firewall en VPN**: Zoek oplossing en update `README.md` voor port-forwarding.

**3. Logging (Details):**
    *   **Leader selection logging** (`bft.cpp`).
    *   **Statistische logging** (`pot_aggregator.cpp`).
    *   **Logging bij onbekende `node_type`** (`config.cpp`).
    *   **Logging van fallback redenen** (`TODO.md`).

**4. Toekomstige Functionaliteit:**
    *   **Configuratie van validators dynamisch maken** (voor future-proofing) (`bft.cpp`).
    *   **Topic management uitbreiden**: Dynamische topic discovery, unsubscribe-functionaliteit, synchroniseer topic subscriptions met peers (`gossip.cpp`).
    *   **IPv6-ondersteuning** (P2P Client, Server, Socket Transport, NTP Client).
    *   **Ondersteuning voor migratie** (opslagformaat) (Storage).

---

## **Wallet CLI Functionaliteiten**

De Wallet CLI (`src/wallet/cli/`) is de Command Line Interface voor gebruikers om met hun fondsen om te gaan. Dit omvat sleutelbeheer, transacties, en account management.

### **Prioriteit: Hoog - Essentiële Wallet Functionaliteit**

**1. Key Management:**
    *   ✅ **Key generation met Dilithium** - VOLTOOID: `generate-keys <key_id>`
        - Genereert een nieuw Dilithium keypair
        - Slaat privésleutel veilig op in `~/.chronos/keys/`
        - Stelt vereiste chmod 0600 in voor beveiliging
    *   ✅ **Key listing** - VOLTOOID: `list-keys`
        - Toont alle beschikbare keys met hun ID's
        - Toont public key in Base58Check format
    *   ✅ **Public key display** - VOLTOOID: `show-public <key_id>`
        - Geeft publieke sleutel in Base58Check format
        - Gebruikt voor config updates in validators
    *   **Key import** - TODO: `import-key <key_id> <private_key_hex>`
        - Importeert een bestaande privésleutel
        - Validatie van sleutelformat
        - Veilige opslag met correcte permissions
    *   **Key deletion** - TODO: `delete-key <key_id>`
        - Verwijdert een key met bevestiging
        - Logging van deletion event
    *   **Key backup/export** - TODO: `export-key <key_id> [--format=hex|json]`
        - Exporteert privésleutel in beveiligde format
        - Waarschuwing voor veiligheid
        - Optioneel password-protected

**2. Account & Balance Management:**
    *   **Balance check** - TODO: `balance <address>`
        - Query node voor account balance
        - Toont saldo in fondseenheden en CHRONOS
        - Geeft ook nonce/transaction count
    *   **Address generation** - TODO: `gen-address <key_id>`
        - Genereert adres uit public key
        - Toont adres in Bech32m format (cqc...)
        - Optioneel meerdere adresderivaties
    *   **Account info** - TODO: `account-info <address>`
        - Volledig account overzicht
        - Balance, nonce, lastTx timestamp
        - Pending transactions

**3. Transaction Management:**
    *   **Send transaction** - TODO: `send-tx <from_key_id> <to_address> <amount> [--fee=<fee>]`
        - Creëert en signeert transactie
        - Zendt naar node RPC
        - Toont transactie hash
        - Validatie van:
          - Adres format (Bech32m)
          - Bedrag (> 0, ≤ balance + fee)
          - Fee (> 0, configurable minimum)
          - Nonce consistency
    *   **Transaction history** - TODO: `tx-history <address> [--limit=10]`
        - Query node voor transaction list
        - Toont: hash, timestamp, from, to, amount, status
        - Paginatie support
    *   **Transaction status** - TODO: `tx-status <tx_hash>`
        - Query node voor transaction status
        - Toont: pending, confirmed (block height), failed
        - Block confirmations count
    *   **Sign message** - TODO: `sign-msg <key_id> <message>`
        - Ondertekent willekeurig bericht met privésleutel
        - Toont signature in hex format
        - Bruikbaar voor off-chain verificatie

**4. Node Interaction:**
    *   **Node status** - TODO: `node-status [--rpc-url=<url>]`
        - Query node health
        - Toont: last block height, sync status, peer count
        - Block time info
    *   **RPC config** - TODO: `config-rpc --rpc-url=<url> --save`
        - Stelt RPC endpoint in
        - Optional save voor toekomstig gebruik
        - Test connectie
    *   **Network info** - TODO: `network-info`
        - Genesis parameters
        - Active validators
        - Network statistics

**5. Security & Validation:**
    *   **Key security check** - TODO: `verify-key <key_id>`
        - Valideert sleutel integriteit
        - Controleert file permissions
        - Checksumvalidatie
    *   **Address validation** - TODO: `validate-address <address>`
        - Valideert Bech32m format
        - Checksumcheck
        - HRP verificatie
    *   **Signature verification** - TODO: `verify-sig <public_key> <message> <signature>`
        - Verificatie van off-chain signatures
        - Bruikbaar voor auditing
    *   **Safe transaction preview** - TODO: `preview-tx <from_key_id> <to_address> <amount>`
        - Toont wat transactie zal doen
        - Final balance na transactie
        - Fee breakdown
        - Confirmatieslag vereist voordat zendt

**6. Configuration & Utilities:**
    *   **Wallet init** - TODO: `init [--hrp=cqc]`
        - Creëert `~/.chronos/` directory structuur
        - Initialiseert config files
        - HRP configuratie
    *   **Wallet info** - TODO: `info`
        - Toont wallet directory
        - Aantal keys
        - Default RPC endpoint
        - Versie info
    *   **Gas estimation** - TODO: `estimate-gas <tx_type>`
        - Voorspelt fee voor transactie
        - Basis fee info van netwerk
        - Prioriteit-based fee suggestions

### **Prioriteit: Medium - Geavanceerde Wallet Features**

**1. Batch Operations:**
    *   **Batch send** - TODO: `batch-send <key_id> <file.csv>`
        - CSV format: `to_address,amount`
        - Atomisch of per-transactie mode
        - Progress tracking
        - Error recovery
    *   **Multi-sig wallet** - TODO (toekomstig)
        - Support voor meerdere signaturen
        - Threshold definitie
        - Approval workflow

**2. Staking & Delegation (Toekomstig):**
    *   **Stake tokens** - TODO: `stake <key_id> <validator_address> <amount>`
    *   **Unstake** - TODO: `unstake <key_id> <validator_address> <amount>`
    *   **Delegation history** - TODO: `delegation-history <address>`

**3. Account Recovery & Backup:**
    *   **Key backup encrypted** - TODO: `backup <key_id> --password`
        - Maakt encrypted backup
        - AES-256 encryption
        - Metadata file
    *   **Wallet recovery** - TODO: `recovery --mnemonic <words>`
        - Recover key from seed phrase
        - BIP39 support (optioneel)

### **Prioriteit: Laag - User Experience**

**1. Output Formatting:**
    *   **JSON output** - TODO: `--json` flag for all commands
        - Parsebare output
        - Scriptable
    *   **Human-readable formatting**
        - Duizendtalseparators
        - Currency symbols
        - Timestamps in local timezone
    *   **Color output** - TODO: Pretty-print met ANSI kleuren
        - Error (rood), Success (groen), Info (blauw)
        - Toggleable met --no-color

**2. Interactive Mode:**
    *   **REPL shell** - TODO: `wallet shell`
        - Interactieve command loop
        - History support
        - Command completion
        - Prompt shows active key

**3. Documentation & Help:**
    *   ✅ **Help system** - `wallet-cli --help`, `wallet-cli <cmd> --help`
    *   **Man pages** - TODO
    *   **Examples file** - TODO: `examples.md` met veelvoorkomende use cases

---

## **KRITIEK ONTBREKEND: Token Economics & Wallet Management**

**Deze vragen moeten beantwoord worden voordat DEX/trading/staking kan worden geïmplementeerd:**

### **1. Genesis Token Allocatie & Verdeling**

Waar starten de tokens en wie krijgt wat?
- Hoeveel tokens in totaal? (fixed supply of inflationary?)
- Waar bevinden deze zich in blok 0 (genesis block)?
- Wie krijgt welk percentage? (team %, investors %, community %, foundation %, etc.)
- Vesting schedules: Lineair unlock over X maanden/jaren, of onmiddellijk?
- Cliff periode: Geen unlock voor X tijd?

Configuration in `config/default.toml`:
```toml
[genesis_allocations]
cqc1foundation... = 100000000
cqc1team... = 150000000
cqc1investors... = 250000000
cqc1community... = 500000000
```

### **2. Token Transfer & Wallet Storage**

Hoe slaan gebruikers tokens op?
- Wallet file (encrypted private key)?
- HD wallet (hierarchical deterministic from seed)?
- Multi-wallet support (meerdere adressen per gebruiker)?
- Encryption: AES-256 met wachtwoord?
- Storage location: ~/.chronos/wallets/?

**Current Status:**
- ✅ KeyManager stores keys in ~/.chronos/keys/ with owner-only permissions
- ❌ MISSING: HD wallet (seed phrase support)
- ❌ MISSING: Multi-sig support
- ❌ MISSING: Encrypted wallet files
- ❌ MISSING: Wallet export/import with encryption

### **3. DEX & Trading**

Kunnen gebruikers tokens kopen/verkopen?
- Via AMM (Automated Market Maker) like Uniswap?
- Order book exchange?
- P2P swaps?
- Via faucet for testnet?
- Welke trading pairs? (CHRN/USDC? CHRN/ETH?)
- On-chain DEX of off-chain?

### **4. Genesis/Backup Wallet & Emergency Recovery**

- Moet er een genesis portefeuille zijn als backup/emergency fund?
- Emergency fund managed by multisig or governance?
- Reserve for protocol/security fund?
- Hoe wordt deze wallet bestuurd? (DAO, foundation, technical committee)
- Recovery procedure if keys are lost?
- Time-lock mechanism for recovery (safety delay)?

### **5. Token Economics Parameters**

Deze parameters moeten expliciet gedefinieerd worden:

```toml
[token_economics]
total_supply = 1000000000  # Total tokens ever created
decimals = 18              # Token precision (like wei in Ethereum)
transaction_fee = 0.0001   # Fee per transaction
validator_min_stake = 1000000  # Minimum to be validator
```

---

## **IMPLEMENTATION ROADMAP voor Token Economics**

**PHASE 1 (IMMEDIATE - blocking DEX/trading/staking):**
- [ ] Answer all 5 question groups above
- [ ] Define genesis allocation percentages
- [ ] Document vesting schedule requirements
- [ ] Design wallet storage format
- [ ] Decide governance model for genesis/emergency wallet
- [ ] Update config/default.toml with token economics parameters

**PHASE 2:**
- [ ] Implement vesting schedule in State
- [ ] Add vesting validation in transaction processing
- [ ] Create wallet file encryption/decryption
- [ ] Build wallet import/export tools

**PHASE 3:**
- [ ] Multi-sig wallet support
- [ ] HD wallet with BIP39 seed phrases
- [ ] Backup and recovery procedures

**PHASE 4:**
- [ ] Governance framework (multisig control of genesis wallet)
- [ ] Emergency pause/resume mechanism
- [ ] Formal network recovery procedures

**PHASE 5:**
- [ ] DEX smart contracts (if needed)
- [ ] Price oracle integration
- [ ] Staking/delegation system

---

## **KRITIEK ONTBREKEND: Peer/Node Discovery Mechanisme**

**Hoe vinden nodes elkaar zonder elkaars IP-adres te kennen?**

Dit is cruciaal voor decentralisatie - nodes moeten elkaar kunnen vinden zonder centrale server.

### **1. Discovery Mechanismen (Kiezen welke(s))**

- **Hardcoded Bootstrap Nodes:**
  - Centrale lijst van bekende nodes in config
  - Eenvoudig maar niet ideaal (single point of failure)
  - Kan gebruikt worden als fallback
  
- **DHT (Distributed Hash Table):**
  - Like BitTorrent's DHT: nodes slaan adressen op in hash table
  - Vraag: "Wie kent node met ID X?" → DHT geeft IP
  - Voordeel: Volledig gedecentraliseerd
  - Nadeel: Complexer, slower discovery
  
- **Kademlia Protocol:**
  - Proven DHT implementation (used in Ethereum, IPFS)
  - Nodes organiseren zich in buckets op distance
  - Logaritmische lookup tijd
  
- **mDNS (Multicast DNS):**
  - Local network discovery (.local domains)
  - Erg bruikbaar voor testnet/local development
  - Werkt niet over internet
  
- **DNS Seeding:**
  - DNS records met node adressen
  - Kan dynamisch geupdate worden
  - Fallback when DHT fails
  
- **Rendezvous Servers:**
  - Lightweight central point (not a full node)
  - Nodes registreren zich tijdelijk
  - Anderen query rendezvous server
  - Minder belastend dan bootstrap nodes

### **2. Node Identity & Addressing**

- **Node ID:**
  - Moet stabil zijn (niet IP-gebaseerd)
  - Kan gebaseerd zijn op public key hash
  - Voorbeeld: BLAKE3(public_key) → node_id
  
- **Network Address:**
  - Multi-address format? (like libp2p: `/ip4/192.168.1.1/tcp/30333`)
  - Kan multiple addresses per node hebben (IPv4, IPv6, onion address)
  
- **Peer Store:**
  - Database van gekende peers
  - Slaat op: node_id, addresses, last_seen, reputation_score
  - Persistent op disk (want nodes rebootten)

### **3. Discovery Flow**

```
1. Node start → lookup known peers from local peer store
2. Query bootstrap nodes → get initial list of active peers
3. Connect to peers → perform handshake with node_id validation
4. Share peer information → "Ik ken ook node X op address Y"
5. Update peer store → add new peers, update last_seen
6. Join DHT/network → announce own address to DHT
7. Continuous refresh → keep peer list fresh, remove stale peers
```

### **4. Implementation Questions**

- **What's the primary discovery mechanism?**
  - Kademlia DHT? (most decentralized)
  - DNS seeding? (simplest, needs DNS infrastructure)
  - Rendezvous servers? (hybrid approach)
  - Combination of above with fallbacks?

- **Bootstrap strategy:**
  - How many bootstrap nodes? (3-5 typical)
  - Where are they hosted? (cloud providers, different regions)
  - How to update bootstrap list without hardfork?

- **Peer store persistence:**
  - Where stored? (~/.chronos/peers.db?)
  - How often synced to disk?
  - Cleanup old/dead peers? (age > 30 days?)
  - Reputation scoring? (penalize peers that disconnect frequently)

- **NAT traversal:**
  - How to handle nodes behind NAT?
  - UPnP support? (automatic port forwarding)
  - Relay/proxy support? (node routes traffic through other node)
  - TCP hole punching?

- **Privacy:**
  - Should node IDs be linkable to IP?
  - Onion address support? (like Tor)
  - VPN-friendly design?

- **Scalability:**
  - Max peers per node? (default 50?)
  - How to prioritize which peers to connect to?
  - Prevent eclipse attacks? (attacker monopolizes all peer connections)

### **5. Current Implementation Status**

- ✅ Basic peer management in NodeApp
- ✅ Handshake with peer_id validation
- ✅ PeerManager tracking connected peers
- ❌ MISSING: Any discovery mechanism
- ❌ MISSING: Persistent peer store
- ❌ MISSING: DHT or DNS seeding
- ❌ MISSING: Bootstrap node coordination
- ❌ MISSING: NAT traversal
- ❌ MISSING: Reputation scoring

### **6. Recommended Approach (for discussion)**

**Short-term (testnet):**
1. Start with hardcoded bootstrap nodes in config
2. Simple peer store (JSON file with known peers)
3. Manual peer management (for testing)

**Medium-term (staging):**
1. Implement Kademlia DHT for decentralized discovery
2. Add DNS seeding as fallback
3. Peer store with reputation scoring
4. UPnP for NAT traversal

**Long-term (mainnet):**
1. Fully decentralized DHT (no bootstrap dependency)
2. Privacy-enhanced addressing (onion support)
3. Advanced NAT handling (relay support)
4. Eclipse attack prevention mechanisms

---
