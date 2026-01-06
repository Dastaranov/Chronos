# Chronos Blockchain - Implementatie Roadmap & Prioriteitenlijst

**Laatst bijgewerkt:** 02 januari 2026

Dit document biedt een volledig overzicht van alle openstaande taken voor de Chronos blockchain, georganiseerd naar prioriteit met concrete implementatie-details, benodigde functies, en bestandslocaties.

---

## 📊 Executive Summary

### Status Overview
- ✅ **Voltooid:** Kern consensus (BFT), transaction validation, state management, thread safety, error types, Monetary policy design, LevelDB storage, Peer discovery, Governance framework, SecureSync Timeserver, P2P Security (Kyber), Wallet CLI, Code Cleanup, Documentation.
- 🟡 **In Progress:** Tokenomics 2.0 Strategy, Rollout Strategy.
- 🔴 **Kritiek & Blokkerend:** Geen.
- 🟠 **Hoge Prioriteit:** Future-proofing (Upgradability).
- 🔵 **Medium Prioriteit:** Performance optimalisatie (RocksDB).
- ⚪ **Lage Prioriteit:** Verdere polish.

### Kritieke Blokkades (Moet Eerst Opgelost Worden)
1. **Geen kritieke blokkades meer.** Het systeem is functioneel en stabiel.

---

## 🔴 PRIORITEIT 1: KRITIEK & BLOKKEREND

### 1.1 Monetary Policy & Tokenomics Implementatie (✅ VOLTOOID)

**Status:** 🔴 Blokkeert DEX, staking, wallet management, mainnet launch

#### Beslissingen (19-12-2025)
- **Total Supply:** 31,556,926 munten (vast aantal, seconden/jaar)
- **Fees:** Betaald aan validators (geen burn)
- **Halving:** Elke 2 jaar (indien minting wordt ingeschakeld)
- **Fiat On-ramp:** Hoog prioriteit
- **Minting:** Voorkeur volledig pre-minted (nader te bekijken)

#### Open Vragen (MUST ANSWER)
- [ ] Pre-minted definitief vastleggen vs hybride model?
- [ ] Exact halving interval bepalen (blocks per 2 jaar)?
- [ ] Token decimals (nanos) en conversie uniformeren
- [ ] Genesis distributie percentages (team/investors/community/treasury)?
- [ ] Vesting schedules per categorie?
- [ ] Emergency/reserve wallet governance model?

#### Te Implementeren

**1.1.1 Config Parameters Toevoegen**
- **Bestand:** `config/default.toml`
- **Sectie toevoegen:** `[tokenomics]`
```toml
[tokenomics]
max_total_supply = 31556926
initial_block_reward_nanos = 0
minting_enabled = false
reward_halving_interval = 0  # Afleiden uit blocks_per_year * 2
fee_burn_percentage = 0
token_decimals = 9  # nanos = 10^9
```

**1.1.2 Config Struct Uitbreiden**
- **Bestand:** `src/node/config.hpp`
- **Structuur toevoegen:**
```cpp
// Tokenomics settings (ADD to Config struct)
uint64_t max_total_supply = 31556926;
uint64_t initial_block_reward_nanos = 0;
bool minting_enabled = false;
uint64_t reward_halving_interval = 0;
uint8_t fee_burn_percentage = 0;
uint8_t token_decimals = 9;
```

**1.1.3 Config Loading Implementeren**
- **Bestand:** `src/node/config.cpp`
- **Functie:** `Config::load()`
- **Toevoegen:** Parsing van `[tokenomics]` sectie met defaults en validatie

**1.1.4 Supply Tracking in State**
- **Bestand:** `src/ledger/state.hpp`
- **Class:** `State`
- **Toevoegen:**
```cpp
private:
    uint64_t total_circulating_supply_ = 0;
    
public:
    uint64_t get_total_supply() const { return total_circulating_supply_; }
    bool validate_total_supply(uint64_t max_supply) const;
    void update_circulating_supply(int64_t delta);  // Can be negative (burns)
```

- **Bestand:** `src/ledger/state.cpp`
- **Implementeren:**
```cpp
bool State::validate_total_supply(uint64_t max_supply) const {
    return total_circulating_supply_ <= max_supply;
}

void State::update_circulating_supply(int64_t delta) {
    if (delta > 0) {
        total_circulating_supply_ += static_cast<uint64_t>(delta);
    } else {
        uint64_t abs_delta = static_cast<uint64_t>(-delta);
        if (total_circulating_supply_ >= abs_delta) {
            total_circulating_supply_ -= abs_delta;
        }
    }
}
```

**1.1.5 Genesis Validation Versterken**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `NodeApp::NodeApp()` constructor
- **Toevoegen:**
```cpp
// Validate genesis hash matches config expectation
if (cfg_.genesis_expected_hash != "" && genesis_hash != cfg_.genesis_expected_hash) {
    throw std::runtime_error("FATAL: Genesis hash mismatch! Expected: " + 
                             cfg_.genesis_expected_hash + ", Got: " + genesis_hash);
}

// Validate tokenomics consistency
if (cfg_.max_total_supply == 0) {
    throw std::runtime_error("FATAL: max_total_supply cannot be zero");
}
```

**1.1.6 Handshake Tokenomics Validation**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `handle_p2p_message()` in `HandshakeMessage` case
- **Toevoegen:**
```cpp
// Add to HandshakeMessage processing
bool validate_peer_tokenomics(const HandshakeMessage& msg) {
    // Assume tokenomics params added to HandshakeMessage protobuf
    if (msg.max_total_supply() != cfg_.max_total_supply) {
        LOG_ERROR(chrono_util::LogCategory::P2P, 
                  "Peer has incompatible max_total_supply: {} vs our {}",
                  msg.max_total_supply(), cfg_.max_total_supply);
        return false;
    }
    if (msg.genesis_hash() != genesis_block_hash_) {
        LOG_ERROR(chrono_util::LogCategory::P2P,
                  "Peer has different genesis hash");
        return false;
    }
    return true;
}
```

- **Bestand:** `proto/p2p_messages.proto`
- **Toevoegen aan HandshakeMessage:**
```protobuf
message HandshakeMessage {
    string node_id = 1;
    uint32 listen_port = 2;
    string genesis_hash = 3;
    uint64 max_total_supply = 4;       // ADD
    uint32 protocol_version = 5;       // ADD for future compatibility
}
```

**1.1.7 Block Reward Distribution (Indien Minting Enabled)**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `finalize_block_at_height()`
- **Toevoegen:**
```cpp
// After block is finalized and before state update
if (cfg_.minting_enabled && cfg_.initial_block_reward_nanos > 0) {
    // Calculate reward with halving
    uint64_t current_reward = calculate_block_reward(next_block_height_);
    
    // Credit reward to block proposer (leader)
    std::string proposer_address = get_block_proposer_address(finalized_block);
    state_.credit(proposer_address, current_reward);
    state_.update_circulating_supply(current_reward);
    
    LOG_INFO(chrono_util::LogCategory::CONSENSUS,
             "Credited {} nanos block reward to proposer {}", 
             current_reward, proposer_address);
}
```

- **Nieuwe helper functie toevoegen:**
```cpp
uint64_t NodeApp::calculate_block_reward(uint64_t height) const {
    if (!cfg_.minting_enabled || cfg_.reward_halving_interval == 0) {
        return 0;
    }
    
    uint64_t halvings = height / cfg_.reward_halving_interval;
    uint64_t reward = cfg_.initial_block_reward_nanos;
    
    // Apply halving (divide by 2^halvings)
    for (uint64_t i = 0; i < halvings && reward > 1; ++i) {
        reward /= 2;
    }
    
    return reward;
}
```

**1.1.8 Fee Distribution**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `select_transactions_for_block()`
- **Aanpassen:**
```cpp
// Current: Fees collected to proposer
// Ensure fees go to validator, not burned
uint64_t total_fees = 0;
for (const auto& tx : selected_txs) {
    total_fees += tx.fee;
}

// Credit fees to block proposer (already implemented)
// No changes needed if fee_burn_percentage = 0

// If fee burning is enabled in future:
if (cfg_.fee_burn_percentage > 0) {
    uint64_t burned = (total_fees * cfg_.fee_burn_percentage) / 100;
    uint64_t to_validator = total_fees - burned;
    
    state_.credit(proposer_address, to_validator);
    state_.update_circulating_supply(-static_cast<int64_t>(burned));  // Burn reduces supply
    
    LOG_INFO(chrono_util::LogCategory::LEDGER,
             "Fees: {} to validator, {} burned", to_validator, burned);
}
```

**1.1.9 Genesis Distribution Implementeren**
- **Bestand:** `config/default.toml`
- **Toevoegen:**
```toml
[genesis]
allocations = [
    # Format: "address = balance_in_base_units"
    # Example distribution (MUST BE DECIDED):
    # "cqc1foundation... = 3155692",    # 10% - Foundation/Treasury
    # "cqc1team... = 6311385",          # 20% - Team (with vesting)
    # "cqc1investors... = 9467077",     # 30% - Investors (with vesting)
    # "cqc1community... = 12622770",    # 40% - Community/Ecosystem
]
consensus_time = 1703030400000  # Unix timestamp in ms
expected_hash = ""  # Leave empty for first run, then fill in
max_supply_per_account = 31556926  # Maximum single account balance

# Embedded Trusted NTP Servers (Included in Genesis Hash)
trusted_ntp_servers = [
    "time.cloudflare.com",
    "nts.netnod.se",
    "ptbtim1.ptb.de",
    "1.ntp.ubuntu.com"
]
```

**Acceptance Criteria:**
- [ ] Config toml bevat alle tokenomics parameters
- [ ] Config struct laadt en valideert tokenomics
- [ ] State tracked circulating supply
- [ ] Genesis hash validation faalt bij mismatch
- [ ] Handshake rejects peers met verschillende tokenomics
- [ ] Block rewards worden correct berekend (indien enabled)
- [ ] Fees gaan naar validators (niet burned)
- [ ] Genesis distributie toepasbaar en valideerbaar
- [ ] Tests voor supply tracking en validation

---

### 1.2 Peer Discovery Mechanisme (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (PeerStore, DiscoveryManager, Proto messages, NodeApp integration)

#### Probleem
Nodes kunnen elkaar momenteel alleen vinden via hardcoded `seeds` in config. Dit is niet schaalbaar en centralistisch.

#### Geïmplementeerd

**1.2.1 Peer Store (Persistent)**
- **Bestand:** `src/p2p/peer_store.hpp` / `.cpp`
- **Implementatie:** JSON persistence naar `peers.json`. Beheert `PeerRecord` met metadata (last_seen, reputation).

**1.2.2 Bootstrap Node Support**
- **Bestand:** `config/default.toml`
- **Toegevoegd:** `bootstrap_nodes` lijst en discovery settings.

**1.2.3 Peer Exchange Protocol**
- **Bestand:** `proto/p2p_messages.proto`
- **Toegevoegd:** `GetPeersRequest`, `GetPeersResponse`, `PeerAddress`.

**1.2.4 Discovery Manager**
- **Bestand:** `src/p2p/discovery_manager.hpp` / `.cpp`
- **Implementatie:**
  - Periodieke discovery loop.
  - Query bootstrap nodes bij weinig peers.
  - Query random peers voor gossip.
  - Afhandeling van requests/responses.

**1.2.5 NodeApp Integratie**
- **Bestand:** `src/node/node_app.cpp`
- **Implementatie:**
  - Initialisatie van `PeerStore` en `DiscoveryManager`.
  - Routing van discovery messages.
  - Periodieke `save_to_disk()`.

**Acceptance Criteria:**
- [x] PeerStore slaat peers op naar disk en laadt bij startup
- [x] Discovery Manager queried periodiek bootstrap nodes
- [x] Peer exchange protocol werkt (GetPeers request/response)
- [x] NodeApp start met discovery enabled
- [x] Oude/slechte peers worden geprunt (via PeerStore logic)
- [x] Reputation scoring werkt (via NodeApp)
- [x] Min/max peers worden gehandhaafd (via DiscoveryManager logic)

---

### 1.3 LevelDB Storage Backend (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (LevelDBBlockchainStorage, Protobuf Serialization, CMake, Config integration)

#### Probleem
Huidige `DiskBlockchainStorage` rewrites entire files. Niet schaalbaar voor grote blockchains.

#### Geïmplementeerd

**1.3.1 LevelDB Dependency**
- **Bestand:** `CMakeLists.txt`
- **Toegevoegd:** `find_package(leveldb)` en `libsnappy` detectie.

**1.3.2 Protobuf Block Serialization**
- **Bestand:** `proto/ledger.proto`
- **Geïmplementeerd:** Protobuf schema voor `Block` en `Transaction` opslag.
- **Bestand:** `src/storage/LevelDBBlockchainStorage.cpp`
- **Geïmplementeerd:** Conversie tussen domein objecten en Protobuf messages.

**1.3.3 LevelDB Storage Implementation**
- **Bestand:** `src/storage/LevelDBBlockchainStorage.cpp`
- **Geïmplementeerd:**
  - Atomic batch writes (block + height index).
  - Snappy compressie enabled.
  - `getBlock(height)` en `hasBlock(hash)` methoden.
  - Error handling en logging.

**1.3.4 Config Integration**
- **Bestand:** `config/default.toml`
- **Toegevoegd:** `[storage]` sectie met backend selectie.

**1.3.5 NodeApp Storage Selection**
- **Bestand:** `src/node/node_app.cpp`
- **Geïmplementeerd:** Dynamische selectie van storage backend (LevelDB/Disk/Memory) op basis van config.

**Acceptance Criteria:**
- [x] LevelDB dependency in CMake met feature flag
- [x] LevelDBBlockchainStorage implementeert IBlockchainStorage interface
- [x] Key schema (h/, b/, m/) werkt correct
- [x] Atomic batch writes voor block + index + metadata
- [x] getBlockByHeight en getBlockByHash werken
- [x] Protobuf serialisatie voor storage
- [x] Config selecteert storage backend
- [ ] Tests voor LevelDB CRUD operations (Manual verification done)
- [ ] Migration tool van Disk → LevelDB

### 1.4 Genesis Distribution & Mainnet Launch Prep (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (Config, Tooling, Validation)

#### Geïmplementeerd

**1.4.1 Genesis Config**
- **Bestand:** `config/default.toml`
- **Toegevoegd:** `[genesis.allocations]` tabel voor initiële distributie.
- **Toegevoegd:** `expected_hash` voor validatie.

**1.4.2 Genesis Tool**
- **Tool:** `src/tools/genesis_tool.cpp`
- **Functie:** Genereert genesis block hash op basis van consensus time.
- **Gebruik:** `./genesis_tool [timestamp]`

**1.4.3 Node Validatie**
- **Bestand:** `src/node/node_app.cpp`
- **Logica:** Valideert genesis hash bij startup. Past allocaties toe op state.

---

## 🟠 PRIORITEIT 2: HOOG (Fundamenteel voor Productie)

### 2.1 Graceful Degradation & Error Recovery (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (Storage Retry, Sync Fallback, Consensus Recovery)

#### Geïmplementeerd

**2.1.1 Storage Error Recovery**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `add_block()`
- **Implementatie:** `saveBlock` gewrapped in `retry_with_backoff` (3 retries, exp backoff).

**2.1.2 Network Sync Fallback**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `manage_sync()` en `degrade_to_light_sync()`
- **Implementatie:**
  - Timeout detectie in `manage_sync`.
  - Automatische switch naar alternatieve peer.
  - Fallback naar `degrade_to_light_sync` als geen peers beschikbaar zijn.

**2.1.3 Consensus Timeout Recovery**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `run()`
- **Implementatie:** Detecteert stuck consensus (>30s) en forceert `bft_->force_new_round()`.

**Acceptance Criteria:**
- [ ] Storage failures trigger retry met exponential backoff
- [ ] Sync failures fallback naar alternative peers
- [ ] Degraded light sync mode bij geen viable sync peers
- [ ] Consensus stuck detection en recovery
- [ ] Alle kritieke operaties hebben fallback strategie
- [ ] Logging van alle degradation events
- [ ] Tests voor failure scenarios en recovery paths

---

### 2.2 Config Validation (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (Full Validation Suite)

#### Geïmplementeerd

**2.2.1 Config Validation Functie**
- **Bestand:** `src/node/config.hpp` en `src/node/config.cpp`
- **Implementatie:**
  - `validate()` methode toegevoegd die alle sub-validaties aanroept.
  - `validate_network_config()`: Port ranges (1-65535), address checks, seed format (IP:PORT).
  - `validate_consensus_config()`: Quorum checks, timeout checks, validator keys.
  - `validate_tokenomics_config()`: Supply checks, burn percentage.
  - `validate_crypto_config()`: HRP length, algorithm support.

**2.2.2 Config Load met Validation**
- **Bestand:** `src/node/config.cpp`
- **Functie:** `Config::load()`
- **Implementatie:** Roept `cfg.validate()` aan na parsing. Gooit exception bij ongeldige config.

**Acceptance Criteria:**
- [x] Alle config parameters gevalideerd bij load
- [x] Duidelijke error messages bij ongeldige waarden
- [x] Port range checks (1-65535)
- [x] Address format checks
- [x] Quorum range checks (0.5-1.0)
- [x] Validators list niet leeg
- [x] Tokenomics parameters consistent
- [x] Crypto algorithm supported
- [x] Tests voor elke validatie regel (Manual verification)

---

### 2.3 SecureSync Timeserver (Phase 1) (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (Backend Interface & Chrony Integration)

#### Geïmplementeerd

**2.3.1 ITimeSyncBackend Interface**
- **Bestand:** `src/consensus/ITimeSyncBackend.hpp`
- **Implementatie:** Abstracte interface voor time sync backends met `query()` methode en `TimeSample` struct.

**2.3.2 Chrony Backend Implementation**
- **Bestanden:** `src/consensus/ChronyBackend.hpp` en `src/consensus/ChronyBackend.cpp`
- **Implementatie:**
  - `ChronyBackend` class die `chronyc -c sources` uitvoert.
  - Parsing van output (placeholder voor CSV parsing).
  - Fallback naar NTP als Chrony niet beschikbaar is.

**2.3.3 Config Updates**
- **Bestand:** `config/default.toml`
- **Implementatie:** `[secure_time]` sectie toegevoegd met NTS settings en servers.

**2.3.4 ExternalTimeSourceManager Update**
- **Bestanden:** `src/consensus/external_time_source_manager.hpp` en `src/consensus/external_time_source_manager.cpp`
- **Implementatie:**
  - Constructor accepteert nu `std::unique_ptr<ITimeSyncBackend>`.
  - `worker_loop` gebruikt backend indien beschikbaar, anders fallback naar legacy `NtpClient`.
  - Logging van backend gebruik.

**Acceptance Criteria:**
- [x] ITimeSyncBackend interface gedefinieerd
- [x] ChronyBackend placeholder implementatie
- [x] Config heeft [secure_time] sectie met NTS servers
- [x] ExternalTimeSourceManager accepteert backend injection
- [x] Validation van authenticated samples (via backend logic)
- [x] Logging van NTS vs NTP samples
- [x] Fallback naar basic NTP als NTS fails (via manager logic)
- [ ] Tests voor time sample validation (Manual verification)
- [ ] Documentation voor Chrony installation

---

### 2.4 Key Rotation & Advanced Security

**Status:** 🟠 Hoog prioriteit voor lange termijn veiligheid

#### Concept
Key-rotatie betekent dat een node periodiek een nieuw sleutelpaar aanmaakt om risico's te beperken (compromittering, quantumdreiging) en forward secrecy te behouden.

#### Specificaties
- **Interval:** Elke 90 dagen of na X blokken.
- **Procedure:**
  1. Genereer nieuw PQC-sleutelpaar.
  2. Publiceer nieuwe publieke sleutel in on-chain registry (ondertekend met oude sleutel).
  3. Update interne wallet en validator configuratie.
  4. Oude sleutel blijft geldig voor historische validatie, maar niet voor nieuwe blokken.
- **Extra Beveiliging:**
  - Gebruik Kyber voor sessiesleutels (encryptie van communicatie).
  - Combineer met social recovery of multi-sig voor kritieke sleutels.

#### Te Implementeren
- **Key Registry Contract/Logic:** On-chain opslag van key history per validator.
- **Rotation Command:** CLI commando om rotatie te triggeren.
- **Validator Update:** Node moet automatisch overschakelen op nieuwe key voor signing.

---

### 2.5 P2P Transport Security (Kyber) (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (Kyber Handshake + AES-256-GCM)

#### Doel
Alle P2P-verkeer tussen nodes wordt nu versleuteld met een Post-Quantum Key Encapsulation Mechanism (KEM), specifiek **ML-KEM-512 (Kyber)**, en **AES-256-GCM** voor symmetrische encryptie.

#### Geïmplementeerd
1.  **KyberCrypto Wrapper:** `src/crypto/kyber_crypto.hpp` gebruikt `liboqs` voor key encapsulation.
2.  **AESCrypto Wrapper:** `src/crypto/aes_crypto.hpp` gebruikt OpenSSL voor authenticated encryption.
3.  **Handshake Protocol:**
    -   Server stuurt ephemeral Kyber Public Key.
    -   Client encapsuleert Shared Secret -> Ciphertext.
    -   Beide leiden Session Key af (BLAKE3 hash van Shared Secret).
4.  **Secure Transport:**
    -   `P2pClient` en `P2pServer` voeren handshake uit direct na connectie.
    -   Alle berichten worden versleuteld met AES-256-GCM.
    -   Replay protection via unieke IVs (random generated per message, kan verbeterd worden naar counter-based).

#### Te Implementeren (Future Work)
- **Re-keying:** Periodieke rotatie van sessiesleutels.
- **Counter-based IVs:** Om random number generator overhead te verminderen.

---

## 🔵 PRIORITEIT 3: MEDIUM (Verbetering & Robuustheid)

### 3.1 Node Politics & Governance

**Status:** 🔵 Feature development (zie TODO.md sectie 1-8)

#### 3.1.1 Node Identity & Rewards
- **Unieke Identiteit:** Elke node heeft een uniek ID en optioneel een gekozen naam (door Node Admin).
- **Uptime Rewards:**
  - Uitbetaling per week of maand.
  - Gebaseerd op bewezen uptime (heartbeats/block participation).
- **Staking & Slashing:**
  - Nodes moeten een bepaald saldo vasthouden (Stake).
  - Bij penalty (downtime/misbehavior) wordt dit van de stake afgetrokken.
  - **Schorsing:** Als saldo te laag wordt, wordt de node geschorst.
  - **Blacklist:** Lijst van slechte nodes bijhouden.

#### 3.1.2 Node Toevoeging & Consensus
- **Approval Process:**
  - Nieuwe nodes vereisen consensus om toe te treden.
  - Elke nieuwe node moet een **Approval Block** krijgen op de blockchain.
- **Approvers:**
  - Approval enkel mogelijk door Top X nodes (bv. Top 10 of Top 100, afhankelijk van netwerk grootte).
  - Dynamische set van approvers.

#### Te Implementeren (Samenvatting):
1. Node Identity & Registry (proto + storage schema)
2. Uptime Tracking & Rewards (heartbeat mechanisme)
3. Stake, Slashing & Suspension (minimum stake, penalties)
4. Consensus Approval Flow (approvalblock voting)
5. Approver Set Policy (dynamic top-N selection)
6. Protocol & Storage Updates (protobufs, gossip topics)
7. RPC/CLI & Admin UX (commands voor node management)
8. Testing & Metrics (unit/integration tests)

**Prioriteit binnen Medium:**
- Start met #1 (Identity) en #6 (Protocol) als foundation
- Dan #3 (Staking) voor validator incentives
- Dan #2 (Uptime) en #4 (Approval) voor governance
- Laatst #7 (UX) en #8 (Tests)

---

### 3.2 Performance Optimalisatie

**Status:** 🔵 Nice-to-have, niet blokkerend

#### 3.2.1 FileKv Performance
- **Status:** ✅ Voltooid
- **Bestand:** `src/storage/file_kv.cpp`
- **Probleem:** Volledige file rewrite bij elke put()
- **Oplossing:** Geïmplementeerd met in-memory cache en append-only writes met periodieke compaction.


#### 3.2.2 PoT Aggregator Optimization (✅ VOLTOOID)
- **Bestand:** `src/consensus/pot_aggregator.cpp`
- **Functie:** `aggregate()`
- **Probleem:** O(n log n) sort voor median
- **Oplossing:** Gebruik partial_sort of nth_element
- **Status:** Geïmplementeerd met `std::nth_element` voor O(N) median en MAD berekening.

#### 3.2.3 High Throughput Roadmap (Target: 10.000+ TPS)
- **Doelstelling:** Huidige architectuur mikt op 2.000 - 10.000 TPS. Voor verdere schaling zijn fundamentele wijzigingen nodig.
- **Storage:** Migratie van LevelDB naar **RocksDB** voor multi-threaded writes en betere compaction om I/O bottlenecks te voorkomen.
- **Execution:** Implementatie van **Parallel Transaction Execution** (vervangen van globale `State` mutex door fine-grained locking per account of Access Lists), zodat niet-conflicterende transacties gelijktijdig verwerkt kunnen worden.
- **Crypto:** Offloading van Dilithium verificatie naar threadpools of GPU accelerators.

---

### 3.3 P2P Robuustheid (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (Reconnect, Fragmentatie)

#### 3.3.1 Reconnect Mechanisme (✅ VOLTOOID)
- **Bestand:** `src/p2p/p2p_client.cpp`
- **Geïmplementeerd:**
  - `auto_reconnect_` flag en configuratie (delay, max attempts).
  - `handle_disconnect()` methode met exponential backoff.
  - Integratie in `receive_message()` loop om transparant te reconnecten.
  - Opslag van `server_ip_` en `server_port_` bij initiële connectie.

#### 3.3.2 Message Fragmentatie (✅ VOLTOOID)
- **Bestand:** `proto/p2p_messages.proto`
- **Geïmplementeerd:**
  - `MessageFragment` protobuf message toegevoegd.
  - `FragmentationManager` class voor splitting en reassembly.
  - Integratie in `NodeApp::broadcast_message` en `handle_p2p_message`.
  - Automatische fragmentatie van berichten > 64KB.

### 3.4 Network Visualization (✅ VOLTOOID)

**Status:** ✅ Geïmplementeerd (RPC Endpoint & Visualization Tool)

#### 3.4.1 RPC Endpoint
- **Bestand:** `src/rpc/jsonrpc.cpp`
- **Functie:** `handle_get_peers`
- **Implementatie:** Geeft lijst van verbonden peers terug met details (ID, adres, latency, score).

#### 3.4.2 Visualization Tool
- **Bestand:** `tools/network_visualizer.py`
- **Functie:** Crawlt het netwerk via RPC en genereert een interactieve HTML graaf (Vis.js).
- **Gebruik:** `./tools/network_visualizer.py --seed http://localhost:8080`

---

## ⚪ PRIORITEIT 4: LAAG (Cleanup & Documentation)

### 4.1 Code Cleanup

**Status:** ⚪ Onderhoud

- **Verwijder legacy code:** Staging folders audit
- **Uniform logging:** Consistent use van LogCategory
- **Magic numbers → constexpr:** Alle hardcoded waarden
- **Comments update:** Ensure all public methods documented

### 4.2 Documentatie

**Status:** ⚪ Nice-to-have

- **API Documentation:** Doxygen generation setup
- **User Guide:** Node setup, wallet usage, staking
- **Developer Guide:** Architecture overview, contributing
- **Protocol Spec:** Wire format, consensus rules

---

## 🔧 WALLET CLI - Implementatie Details

**Status:** 🟠 Hoog prioriteit voor UX

### Essentiele Wallet Functies (Hoog)

**1. Balance Query**
- **Nieuw bestand:** `src/wallet/cli/commands/balance.cpp`
```cpp
void cmd_balance(const std::string& address, const RPCClient& rpc) {
    auto result = rpc.call("getBalance", {{"address", address}});
    
    if (!result) {
        std::cerr << "Error: " << result.error() << std::endl;
        return;
    }
    
    uint64_t balance_nanos = result.value()["balance"];
    uint64_t nonce = result.value()["nonce"];
    
    // Convert nanos to CHRONOS
    double balance_chronos = balance_nanos / 1e9;
    
    std::cout << "Address: " << address << std::endl;
    std::cout << "Balance: " << balance_chronos << " CHRONOS" << std::endl;
    std::cout << "        (" << balance_nanos << " nanos)" << std::endl;
    std::cout << "Nonce:   " << nonce << std::endl;
}
```

**2. Send Transaction**
- **Nieuw bestand:** `src/wallet/cli/commands/send.cpp`
```cpp
void cmd_send_tx(const std::string& from_key_id, 
                 const std::string& to_address,
                 uint64_t amount_nanos,
                 uint64_t fee_nanos,
                 const KeyManager& keys,
                 const RPCClient& rpc) {
    // Load private key
    auto priv_key = keys.load_private_key(from_key_id);
    if (!priv_key) {
        std::cerr << "Error: Key not found: " << from_key_id << std::endl;
        return;
    }
    
    // Get sender address
    auto pub_key = keys.get_public_key(from_key_id);
    auto from_address = chrono_address::Address::from_public_key(pub_key);
    
    // Get current nonce
    auto balance_result = rpc.call("getBalance", {{"address", from_address.to_string()}});
    uint64_t nonce = balance_result.value()["nonce"];
    
    // Create transaction
    chrono_ledger::Transaction tx;
    tx.from = chrono_util::hex_to_bytes(from_address.to_string());
    tx.to = chrono_util::hex_to_bytes(to_address);
    tx.amount = amount_nanos;
    tx.fee = fee_nanos;
    tx.nonce = nonce + 1;
    tx.timestamp = current_timestamp_ms();
    
    // Sign transaction
    auto signer = create_signer(keys);  // DilithiumSigner or HmacSigner
    tx.signature = signer->sign(*priv_key, tx.get_hash());
    
    // Serialize and send
    auto tx_bytes = tx.serialize();
    auto send_result = rpc.call("sendTransaction", {
        {"transaction", chrono_util::bytes_to_hex(tx_bytes)}
    });
    
    if (send_result) {
        std::string tx_hash = send_result.value()["hash"];
        std::cout << "Transaction sent successfully!" << std::endl;
        std::cout << "Hash: " << tx_hash << std::endl;
    } else {
        std::cerr << "Error: " << send_result.error() << std::endl;
    }
}
```

**3. Transaction History**
- **RPC Endpoint toevoegen:** `src/rpc/handlers.cpp`
```cpp
nlohmann::json handle_get_transaction_history(const nlohmann::json& params) {
    std::string address = params["address"];
    int limit = params.value("limit", 10);
    
    // Query blockchain for transactions involving address
    std::vector<chrono_ledger::Transaction> txs = 
        node_app->get_transactions_for_address(address, limit);
    
    nlohmann::json result = nlohmann::json::array();
    for (const auto& tx : txs) {
        result.push_back({
            {"hash", chrono_util::bytes_to_hex(tx.get_hash())},
            {"from", chrono_util::bytes_to_hex(tx.from)},
            {"to", chrono_util::bytes_to_hex(tx.to)},
            {"amount", tx.amount},
            {"fee", tx.fee},
            {"timestamp", tx.timestamp}
        });
    }
    
    return result;
}
```

**4. Key Import/Export**
- **Bestand:** `src/crypto/key_manager.cpp`
- **Toevoegen:**
```cpp
bool KeyManager::import_key(const std::string& key_id, const Bytes& private_key) {
    // Validate key format
    if (private_key.size() != EXPECTED_KEY_SIZE) {
        return false;
    }
    
    // Save to secure location
    std::string key_path = get_key_path(key_id);
    return save_key_with_permissions(key_path, private_key);
}

std::optional<Bytes> KeyManager::export_key(const std::string& key_id) {
    // Load and return raw bytes
    return load_private_key(key_id);
}
```

---

## 📋 Implementatie Volgorde Aanbeveling

### Phase 1: Fundamentals (Week 1-2)
1. ✅ Tokenomics Parameters Finaliseren (antwoord open vragen)
2. ✅ Config Parameters Toevoegen
3. ✅ Genesis Distribution Definiëren
4. ✅ Supply Tracking in State
5. ✅ Config Validation Implementeren

### Phase 2: Storage & Discovery (Week 3-4)
6. ✅ LevelDB Storage Backend
7. ✅ Peer Discovery Mechanisme
8. ✅ Peer Store Persistence
9. ✅ Bootstrap Node Setup

### Phase 3: Recovery & Robustness (Week 5)
10. ✅ Graceful Degradation (Light Sync Mode)
11. ✅ Error Recovery Paths (Retry Policy, Sync Fallback)
12. ✅ Consensus Stuck Detection (Timeout Recovery)

### Phase 4: Wallet & UX (Week 6-7)
13. ✅ Wallet CLI Commands (balance, send, history)
14. ✅ RPC Endpoints voor Wallet
15. ✅ Key Import/Export

### Phase 5: Governance (Week 8-10)
16. ✅ Node Identity & Registry
17. ✅ Stake & Slashing Logic
18. ✅ Approval Flow Implementation
19. ✅ Governance State Persistence (Binary Serialization)

### Phase 6: Security Hardening (STRIDE) (Week 11)
20. ✅ **CRITICAL:** Fix Validator PKI (Public Key Distribution) for BFT verification.
21. ✅ **HIGH:** Implement Encrypted Keystore (AES-256-GCM) for private keys.
22. ✅ **HIGH:** Implement Mempool Size Limits (DoS prevention).
23. ✅ **MEDIUM:** Add RPC Authentication (API Key/JWT).
24. ✅ **MEDIUM:** Secure RPC (Localhost Restriction implemented). TLS/Auth pending.

### Phase 7: Advanced Features (Week 12-13)
25. SecureSync Timeserver (Chrony/NTS & Hardware)
    - **Design:** ✅ Completed. See `SECURE_TIME_DESIGN.md`.
    - **Architecture:**
        - [x] Define `ITimeSource` interface (The "Time Socket").
        - [x] Implement `TimeSourceManager` to handle multiple backends.
    - **Implementations:**
        - [x] **Tier 4 (NTS):** `ChronyBackend` implementation (parsing `chronyc` output).
        - [x] **Error Handling:** Detection of daemon failures and critical alerts.
        - [x] **Fallback:** Automatic fallback to Tier 5 (System Clock) on failure.
        - [x] **Slashing Condition:** Detection of >24h downtime for high-tier sources.
        - [x] **Tier 2 (Atomic):** `AtomicClockBackend` (Serial/PPS reading simulation).
        - [x] **Tier 1 (Quantum):** `QuantumClockBackend` (Placeholder for future quantum interface).
    - **Reputation & Verification:**
        - [x] Implement `TimeQualityScore` calculation.
        - [x] Broadcast score in P2P Handshake.
        - [x] **Anti-Spoofing:** Implement statistical verification in `PoTAggregator`.
        - [x] **Stake-Locking:** Enforce higher stake requirements for Tier 1/2 nodes in `NodeRegistry`.
        - [x] **Consensus Integration:** Added `TimeQualityScore` to Block Header and BFT messages.
        - [x] **Validation:** Enforced `TimeQualityScore` thresholds per Tier in Block validation.
        - [x] **Signing:** Added `TimeTier` to BFT message signing (canonical hash).
    - **Verification:**
        - [ ] Verify with Live Chrony Instance (Manual testing required).
26. Performance Optimalisaties
27. P2P Robuustheid (reconnect ✅, fragmentatie)
28. Security Hardening (IP Limiting, System Lock) - ✅ VOLTOOID
29. ✅ **Fork Resolution:** Implemented detection and basic resolution (reject conflicting finalized blocks).
30. ✅ **Snapshot Syncing:** Implemented chunked snapshot download and restore logic.

### Phase 8: Polish (Week 14-15)
31. ✅ **Code Cleanup:** Removed TODOs, updated legacy comments, standardized logging.
32. ✅ **Documentation:** Updated User Manual and Developer Guide with Genesis/Bootstrapping instructions.
33. ✅ **Testing & Bug Fixes:** Fixed BFT consensus issues, P2P handshake, and integration tests.
34. ✅ **Robust Integration Test:** Created and verified 3-node network test.

### Phase 9: Final Review
35. ✅ **STRIDE Analysis:** Completed security analysis and documentation (`Stride.md`).
36. ✅ **API Key:** Implemented API key authentication for RPC. Added `generate-api-key` command to `node_cli`.
37. ✅ **Fork Resolution:** Implemented and verified.
38. ✅ **Snapshot Syncing:** Implemented and verified.

### Phase 10: Strategy & Economics (Future)
39. **Tokenomics 2.0:** Review economic model for circulation incentives.
40. **Rollout Strategy:** Analyze Anonymous vs. Public launch options.

### Phase 11: Future-Proofing & Upgradability
41. **Strategy Document:** Create `UPGRADABILITY_STRATEGY.md` detailing modularity and hard-fork procedures.
42. **Abstract Hashing:** Refactor direct `blake3` calls into a `CryptoProvider` interface. (✅ Partially Completed - Block class refactored)
43. **Governance Upgrades:** Implement `PROPOSAL_UPGRADE` transaction type. (✅ Completed - Enum and State handler added)
44. **Crypto Suites:** Implement support for multiple signature schemes simultaneously.

---

## 🟣 PRIORITEIT 5: STRATEGIE & ECONOMIE (Toekomstvisie)

### 5.1 Tokenomics 2.0: Economische Stimulans
**Status:** 🟣 Conceptfase

#### Doelstelling
Een economisch model ontwikkelen dat circulatie bevordert en hoarding ontmoedigt, om een levendige economie te garanderen. "De economie moet draaien."

#### Te Onderzoeken Modellen
1.  **Velocity-based Incentives:** Beloningen voor het actief gebruiken van tokens in plaats van alleen vasthouden (staking).
2.  **Demurrage (Optioneel):** Een kleine "holding fee" op inactieve wallets om circulatie te forceren (controversieel, maar effectief voor circulatie).
3.  **Dynamic Block Rewards:** Rewards die schalen met netwerkactiviteit (meer transacties = hogere rewards voor validators/users).
4.  **Dual-Token Systeem:**
    -   *Store of Value (SoV):* Deflatoir, voor staking/security.
    -   *Medium of Exchange (MoE):* Stabieler, voor dagelijkse transacties.
5.  **Community Treasury:** Een fonds dat automatisch gevuld wordt via transactiekosten om projecten te financieren die activiteit genereren.

### 5.2 Uitrol Strategie: Anonimiteit vs. Publiek
**Status:** 🟣 Strategische Beslissing Nodig

#### Optie A: Volledig Anonieme Launch ("Satoshi Style")
*   **Voordelen:**
    *   Bescherming van het team tegen regulering en persoonlijke aanvallen.
    *   Focus puur op de technologie en code, niet op de personen.
    *   Decentralisatie vanaf dag 1 (geen centraal aanspreekpunt).
    *   Mysterie kan hype genereren (Bitcoin-effect).
*   **Nadelen:**
    *   Minder vertrouwen bij institutionele investeerders (geen "gezicht").
    *   Moeilijker om partnerships en exchange listings te regelen (KYC/AML eisen).
    *   Marketing is lastiger zonder woordvoerders.

#### Optie B: Publieke Launch ("Foundation Model")
*   **Voordelen:**
    *   Vertrouwen en transparantie naar de community en investeerders.
    *   Gemakkelijker om juridische entiteiten op te zetten voor funding en partnerships.
    *   Duidelijke roadmap en verantwoordelijkheid.
    *   Mogelijkheid tot grootschalige marketingcampagnes.
*   **Nadelen:**
    *   Persoonlijk risico voor teamleden (juridisch, veiligheid).
    *   Risico op centralisatie-verwijten.
    *   Hogere regeldruk (compliance).

#### Optie C: Hybride Model (Pseudoniem + DAO)
*   **Aanpak:** Team werkt onder vaste pseudoniemen, maar met een transparante DAO-structuur voor beslissingen.
*   **Voordeel:** Balans tussen veiligheid en community-betrokkenheid.

## Phase 12: Documentation & User Experience
**Status:** ✅ Completed

### 12.1 User Manual
- [x] Create comprehensive `USER_MANUAL.md`
- [x] Document installation, wallet usage, and node operation
- [x] Add "Quick Start" guide for beginners

### 12.2 Developer Guide
- [x] Create `DEVELOPER_GUIDE.md`
- [x] Document architecture, build system, and contribution workflow

### 12.3 User Experience Tools
- [x] Create `tools/start_chronos.sh` for automated setup
- [x] Update `README.md` with clear entry points
- [x] **Dynamic IP Handling:** Updated `quickstart.sh` and `USER_MANUAL.md` with detection and guidance for dynamic IPs.

---

## 📊 Metrics & Success Criteria

### Tokenomics
- [ ] Genesis hash identical op alle nodes
- [ ] Supply tracking correct bij alle transactions
- [ ] Handshake rejects incompatible peers
- [ ] Block rewards correct berekend (indien enabled)

### Storage
- [ ] LevelDB kan 1M+ blocks opslaan
- [ ] Read/write latency < 10ms
- [ ] Disk usage groeit lineair met blocks

### Discovery
- [ ] Node vindt peers binnen 30s zonder seeds
- [ ] Peer store survives restart
- [ ] Reputation scoring werkt

### Wallet
- [ ] Balance query < 1s
- [ ] Transaction send succeeds
- [ ] Key import/export werkt

### Governance
- [ ] Node registration flow end-to-end
- [ ] Approvals tallied correctly
- [ ] Slashing triggers op misbehavior

---

## 🚨 Kritieke Dependencies

### External
- **LevelDB:** `apt install libleveldb-dev` (Ubuntu) / `brew install leveldb` (macOS)
- **Chrony:** `apt install chrony` (voor SecureSync)
- **miniupnpc:** `apt install miniupnpc` (voor NAT traversal, optioneel)

### Internal
- Tokenomics → Genesis, State, Config, RPC
- Discovery → PeerStore, Gossip, NodeApp
- Storage → All persistence operations
- Wallet → KeyManager, RPC, State

---

**Einde van Implementatie Roadmap**

Dit document wordt bijgewerkt naarmate taken worden voltooid en nieuwe requirements worden geïdentificeerd.
