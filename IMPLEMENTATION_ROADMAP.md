# Chronos Blockchain - Implementatie Roadmap & Prioriteitenlijst

**Laatst bijgewerkt:** 19 december 2025

Dit document biedt een volledig overzicht van alle openstaande taken voor de Chronos blockchain, georganiseerd naar prioriteit met concrete implementatie-details, benodigde functies, en bestandslocaties.

---

## 📊 Executive Summary

### Status Overview
- ✅ **Voltooid:** Kern consensus (BFT), transaction validation, state management, thread safety, error types
- 🟡 **In Progress:** Monetary policy design, governance framework
- 🔴 **Kritiek & Blokkerend:** LevelDB storage, peer discovery, tokenomics parameters, wallet features
- 🟠 **Hoge Prioriteit:** Config validation, graceful degradation, SecureSync timeserver
- 🔵 **Medium Prioriteit:** Performance optimalisatie, P2P robuustheid
- ⚪ **Lage Prioriteit:** Code cleanup, documentatie updates

### Kritieke Blokkades (Moet Eerst Opgelost Worden)
1. **Tokenomics Parameters Finaliseren** - Blokkeert DEX, staking, wallet integratie
2. **Peer Discovery Mechanisme** - Blokkeert decentralisatie en productie deployment
3. **LevelDB Storage Backend** - Blokkeert schaalbaarheid en performance
4. **Genesis Distribution** - Blokkeert mainnet launch

---

## 🔴 PRIORITEIT 1: KRITIEK & BLOKKEREND

### 1.1 Monetary Policy & Tokenomics Implementatie

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

### 1.2 Peer Discovery Mechanisme

**Status:** 🔴 Blokkeert decentralisatie, productie deployment, mainnet

#### Probleem
Nodes kunnen elkaar momenteel alleen vinden via hardcoded `seeds` in config. Dit is niet schaalbaar en centralistisch.

#### Te Implementeren

**1.2.1 Peer Store (Persistent)**
- **Nieuw bestand:** `src/p2p/peer_store.hpp`
```cpp
#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>

namespace chrono_p2p {

struct PeerRecord {
    std::string node_id;
    std::vector<std::string> addresses;  // Can have multiple IPs
    std::chrono::system_clock::time_point last_seen;
    int reputation_score = 0;
    bool is_validator = false;
};

class PeerStore {
public:
    PeerStore(const std::string& db_path);
    
    void add_peer(const PeerRecord& peer);
    void update_last_seen(const std::string& node_id);
    void update_reputation(const std::string& node_id, int delta);
    
    std::vector<PeerRecord> get_all_peers() const;
    std::vector<PeerRecord> get_recent_peers(int max_age_hours = 24) const;
    std::optional<PeerRecord> get_peer(const std::string& node_id) const;
    
    void remove_peer(const std::string& node_id);
    void prune_old_peers(int max_age_days = 30);
    
    void save_to_disk();
    void load_from_disk();
    
private:
    std::string db_path_;
    std::unordered_map<std::string, PeerRecord> peers_;
};

} // namespace chrono_p2p
```

- **Nieuw bestand:** `src/p2p/peer_store.cpp`
- **Implementatie:** JSON persistence naar `~/.chronos/peers.json`

**1.2.2 Bootstrap Node Support**
- **Bestand:** `config/default.toml`
- **Aanpassen:**
```toml
[network]
# Bootstrap nodes: well-known nodes for initial discovery
bootstrap_nodes = [
    "bootstrap1.chronoschain.io:8645",
    "bootstrap2.chronoschain.io:8645",
    "bootstrap3.chronoschain.io:8645"
]

# Legacy seeds for backwards compatibility
seeds = ["127.0.0.1:8645"]

# Peer discovery settings
enable_peer_discovery = true
max_peers = 50
min_peers = 10
peer_discovery_interval_ms = 30000  # Query for new peers every 30s
```

**1.2.3 Peer Exchange Protocol**
- **Bestand:** `proto/p2p_messages.proto`
- **Toevoegen:**
```protobuf
message GetPeersRequest {
    uint32 max_peers = 1;  // How many peer addresses to return
}

message GetPeersResponse {
    repeated PeerAddress peers = 1;
}

message PeerAddress {
    string node_id = 1;
    string address = 2;    // IP:port format
    uint64 last_seen = 3;  // Unix timestamp
    bool is_validator = 4;
}

// Update P2PMessage to include new types
message P2PMessage {
    oneof payload {
        // ... existing messages ...
        GetPeersRequest get_peers_request = 10;
        GetPeersResponse get_peers_response = 11;
    }
}
```

**1.2.4 Discovery Manager**
- **Nieuw bestand:** `src/p2p/discovery_manager.hpp`
```cpp
#pragma once
#include "peer_store.hpp"
#include "gossip.hpp"
#include <memory>

namespace chrono_p2p {

class DiscoveryManager {
public:
    DiscoveryManager(
        std::shared_ptr<PeerStore> peer_store,
        std::shared_ptr<Gossip> gossip,
        const std::vector<std::string>& bootstrap_nodes
    );
    
    void start();
    void stop();
    
    // Periodic discovery tick
    void discover_peers();
    
    // Handle incoming peer exchange
    void handle_get_peers_request(const std::string& from_peer_id, uint32_t max_peers);
    void handle_get_peers_response(const std::vector<PeerAddress>& peers);
    
private:
    std::shared_ptr<PeerStore> peer_store_;
    std::shared_ptr<Gossip> gossip_;
    std::vector<std::string> bootstrap_nodes_;
    
    bool running_ = false;
    std::thread discovery_thread_;
    
    void discovery_loop();
    void query_bootstrap_nodes();
    void query_random_peers();
};

} // namespace chrono_p2p
```

**1.2.5 NodeApp Integratie**
- **Bestand:** `src/node/node_app.hpp`
- **Toevoegen:**
```cpp
#include "p2p/peer_store.hpp"
#include "p2p/discovery_manager.hpp"

private:
    std::shared_ptr<chrono_p2p::PeerStore> peer_store_;
    std::shared_ptr<chrono_p2p::DiscoveryManager> discovery_manager_;
```

- **Bestand:** `src/node/node_app.cpp`
- **Constructor aanpassen:**
```cpp
NodeApp::NodeApp(const Config& cfg) : cfg_(cfg) {
    // ... existing initialization ...
    
    // Initialize peer store
    std::string peer_db_path = cfg_.data_dir + "/peers.json";
    peer_store_ = std::make_shared<chrono_p2p::PeerStore>(peer_db_path);
    peer_store_->load_from_disk();
    
    // Initialize discovery manager
    discovery_manager_ = std::make_shared<chrono_p2p::DiscoveryManager>(
        peer_store_,
        gossip_,
        cfg_.bootstrap_nodes
    );
    
    // Start discovery
    if (cfg_.enable_peer_discovery) {
        discovery_manager_->start();
    }
}
```

- **Main loop aanpassen:**
```cpp
void NodeApp::run() {
    while (running_) {
        // ... existing logic ...
        
        // Periodic peer management
        if (should_manage_peers()) {
            manage_peers();
            peer_store_->save_to_disk();  // Persist changes
        }
    }
}
```

**1.2.6 NAT Traversal (UPnP)**
- **Nieuw bestand:** `src/p2p/upnp_client.hpp`
- **Implementatie:** UPnP port forwarding via miniupnpc library
- **Bestand:** `CMakeLists.txt`
- **Toevoegen:** `find_package(miniupnpc)` dependency (optioneel)

**Acceptance Criteria:**
- [ ] PeerStore slaat peers op naar disk en laadt bij startup
- [ ] Discovery Manager queried periodiek bootstrap nodes
- [ ] Peer exchange protocol werkt (GetPeers request/response)
- [ ] NodeApp start met discovery enabled
- [ ] Oude/slechte peers worden geprunt
- [ ] Reputation scoring werkt
- [ ] Min/max peers worden gehandhaafd
- [ ] Tests voor peer store persistence en discovery flow

---

### 1.3 LevelDB Storage Backend

**Status:** 🔴 Blokkeert schaalbaarheid, performance, mainnet

#### Probleem
Huidige `DiskBlockchainStorage` rewrites entire files. Niet schaalbaar voor grote blockchains.

#### Te Implementeren

**1.3.1 LevelDB Dependency**
- **Bestand:** `CMakeLists.txt`
- **Toevoegen:**
```cmake
# LevelDB dependency
find_package(leveldb REQUIRED)
if(leveldb_FOUND)
    set(CHRONOS_USE_LEVELDB ON)
    message(STATUS "LevelDB found, enabling LevelDBBlockchainStorage")
else()
    set(CHRONOS_USE_LEVELDB OFF)
    message(WARNING "LevelDB not found, LevelDB storage disabled")
endif()
```

**1.3.2 Protobuf Block Serialization**
- **Bestand:** `proto/p2p_messages.proto`
- **Aanpassen:** Hergebruik bestaande `BlockMessage` voor storage
- **Alternatief:** Nieuwe `proto/storage.proto` voor storage-specifieke types

**1.3.3 LevelDB Storage Implementation**
- **Nieuw bestand:** `src/storage/LevelDBBlockchainStorage.hpp`
```cpp
#pragma once
#include "IBlockchainStorage.hpp"
#include <leveldb/db.h>
#include <memory>

namespace chrono_storage {

class LevelDBBlockchainStorage : public IBlockchainStorage {
public:
    LevelDBBlockchainStorage(const std::string& db_path);
    ~LevelDBBlockchainStorage() override;
    
    bool saveBlock(const chrono_ledger::Block& block) override;
    std::optional<chrono_ledger::Block> getBlockByHash(const Bytes& hash) override;
    std::optional<chrono_ledger::Block> getBlockByHeight(uint64_t height) override;
    bool hasBlock(const Bytes& hash) const override;
    
    bool saveMetadata(const BlockchainMetadata& meta) override;
    std::optional<BlockchainMetadata> getMetadata() override;
    
    // Batch operations for sync
    bool appendBlocks(const std::vector<chrono_ledger::Block>& blocks);
    
    // Integrity validation
    bool validateBlockChecksum(const Bytes& hash) const;
    bool validateSegmentMerkleRoot(uint64_t segment_start, uint64_t segment_end) const;
    
private:
    std::unique_ptr<leveldb::DB> db_;
    
    // Key schema helpers
    std::string height_key(uint64_t height) const;  // "h/<height>"
    std::string block_key(const Bytes& hash) const; // "b/<hash>"
    std::string meta_key() const;                   // "m/meta"
    
    // Serialization
    Bytes serialize_block_with_checksum(const chrono_ledger::Block& block) const;
    std::optional<chrono_ledger::Block> deserialize_block_with_validation(const Bytes& data) const;
};

} // namespace chrono_storage
```

- **Nieuw bestand:** `src/storage/LevelDBBlockchainStorage.cpp`
- **Implementeren:**
```cpp
#include "LevelDBBlockchainStorage.hpp"
#include "../ledger/block.hpp"
#include "../util/log.hpp"
#include "../util/codec.hpp"  // For LE encoding
#include <leveldb/write_batch.h>
#include <blake3.h>

namespace chrono_storage {

LevelDBBlockchainStorage::LevelDBBlockchainStorage(const std::string& db_path) {
    leveldb::Options options;
    options.create_if_missing = true;
    options.compression = leveldb::kSnappyCompression;
    
    leveldb::DB* db_raw;
    leveldb::Status status = leveldb::DB::Open(options, db_path, &db_raw);
    
    if (!status.ok()) {
        throw std::runtime_error("Failed to open LevelDB: " + status.ToString());
    }
    
    db_.reset(db_raw);
    LOG_INFO(chrono_util::LogCategory::STORAGE, "LevelDB opened at {}", db_path);
}

bool LevelDBBlockchainStorage::saveBlock(const chrono_ledger::Block& block) {
    // Serialize block with checksum
    Bytes block_data = serialize_block_with_checksum(block);
    Bytes block_hash = block.get_header_hash();
    uint64_t height = block.height;
    
    // Atomic batch write: block data + height index + metadata update
    leveldb::WriteBatch batch;
    
    // Write block: "b/<hash>" -> block_data
    batch.Put(block_key(block_hash), 
              leveldb::Slice(reinterpret_cast<const char*>(block_data.data()), block_data.size()));
    
    // Write height index: "h/<height>" -> hash
    std::string height_k = height_key(height);
    batch.Put(height_k,
              leveldb::Slice(reinterpret_cast<const char*>(block_hash.data()), block_hash.size()));
    
    // Update metadata (tip)
    BlockchainMetadata meta;
    meta.last_block_hash = block_hash;
    meta.last_block_height = height;
    // ... serialize meta ...
    batch.Put(meta_key(), /* serialized meta */);
    
    leveldb::Status status = db_->Write(leveldb::WriteOptions(), &batch);
    
    if (!status.ok()) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to save block: {}", status.ToString());
        return false;
    }
    
    return true;
}

std::optional<chrono_ledger::Block> LevelDBBlockchainStorage::getBlockByHeight(uint64_t height) {
    // Read height index to get hash
    std::string value;
    leveldb::Status status = db_->Get(leveldb::ReadOptions(), height_key(height), &value);
    
    if (!status.ok()) {
        return std::nullopt;
    }
    
    Bytes block_hash(value.begin(), value.end());
    return getBlockByHash(block_hash);
}

// ... implement other methods ...

} // namespace chrono_storage
```

**1.3.4 Config Integration**
- **Bestand:** `config/default.toml`
- **Toevoegen:**
```toml
[storage]
backend = "leveldb"  # Options: "leveldb", "disk", "memory"
leveldb_path = "data/leveldb"
compression_enabled = true
```

- **Bestand:** `src/node/config.hpp`
```cpp
std::string storage_backend = "leveldb";
std::string leveldb_path = "data/leveldb";
bool storage_compression_enabled = true;
```

**1.3.5 NodeApp Storage Selection**
- **Bestand:** `src/node/node_app.cpp`
- **Constructor aanpassen:**
```cpp
// Storage initialization based on config
if (cfg_.storage_backend == "leveldb") {
#ifdef CHRONOS_USE_LEVELDB
    blockchain_storage_ = std::make_shared<chrono_storage::LevelDBBlockchainStorage>(
        cfg_.leveldb_path
    );
#else
    throw std::runtime_error("LevelDB not available, rebuild with LevelDB support");
#endif
} else if (cfg_.storage_backend == "disk") {
    blockchain_storage_ = std::make_shared<chrono_storage::DiskBlockchainStorage>(
        cfg_.data_dir
    );
} else if (cfg_.storage_backend == "memory") {
    blockchain_storage_ = std::make_shared<chrono_storage::MemoryBlockchainStorage>();
} else {
    throw std::runtime_error("Unknown storage backend: " + cfg_.storage_backend);
}
```

**Acceptance Criteria:**
- [ ] LevelDB dependency in CMake met feature flag
- [ ] LevelDBBlockchainStorage implementeert IBlockchainStorage interface
- [ ] Key schema (h/, b/, m/) werkt correct
- [ ] Atomic batch writes voor block + index + metadata
- [ ] getBlockByHeight en getBlockByHash werken
- [ ] Checksum validatie bij read
- [ ] appendBlocks voor batch import tijdens sync
- [ ] Config selecteert storage backend
- [ ] Tests voor LevelDB CRUD operations
- [ ] Migration tool van Disk → LevelDB

---

## 🟠 PRIORITEIT 2: HOOG (Fundamenteel voor Productie)

### 2.1 Graceful Degradation & Error Recovery

**Status:** 🟠 Essentieel voor robuustheid

#### Te Implementeren

**2.1.1 Storage Error Recovery**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `finalize_block_at_height()`
- **Aanpassen:**
```cpp
void NodeApp::finalize_block_at_height(uint64_t height) {
    // Wrap in retry logic
    auto save_result = chrono_util::retry_on_error<bool>(
        [this, height]() -> chrono_util::Result<bool> {
            bool success = blockchain_storage_->saveBlock(finalized_block);
            if (!success) {
                return chrono_util::Result<bool>::error(
                    chrono_util::StorageErrorCode::IOError,
                    "saveBlock",
                    "storage",
                    "Failed to persist block at height " + std::to_string(height)
                );
            }
            return chrono_util::Result<bool>::ok(true);
        },
        chrono_util::RetryPolicy{.max_retries = 3, .initial_backoff_ms = 100}
    );
    
    if (!save_result) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE,
                  "Failed to save block after retries: {}", save_result.error.to_string());
        // Fallback: mark block as "pending persistence" and retry later
        pending_blocks_.push_back(finalized_block);
        return;
    }
}
```

**2.1.2 Network Sync Fallback**
- **Bestand:** `src/node/node_app.cpp`
- **Functie:** `start_sync_with_peer()`
- **Aanpassen:**
```cpp
void NodeApp::start_sync_with_peer(const std::string& peer_id) {
    auto sync_result = chrono_util::retry_on_error<bool>(
        [this, peer_id]() -> chrono_util::Result<bool> {
            // Attempt sync with peer
            // ... existing sync logic ...
            
            if (sync_failed) {
                return chrono_util::Result<bool>::error(
                    chrono_util::NetworkErrorCode::PeerDisconnected,
                    "sync_with_peer",
                    "p2p",
                    "Sync failed with peer " + peer_id
                );
            }
            return chrono_util::Result<bool>::ok(true);
        },
        chrono_util::RetryPolicy{.max_retries = 2, .initial_backoff_ms = 1000}
    );
    
    if (!sync_result) {
        // Fallback: Try different peer
        LOG_WARN(chrono_util::LogCategory::P2P,
                 "Sync failed with {}, trying alternative peer", peer_id);
        
        auto alternative_peer = select_best_sync_peer_excluding(peer_id);
        if (alternative_peer) {
            start_sync_with_peer(*alternative_peer);
        } else {
            // Ultimate fallback: switch to light mode temporarily
            LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                     "No viable sync peers, degrading to light sync mode");
            degrade_to_light_sync();
        }
    }
}

void NodeApp::degrade_to_light_sync() {
    // Temporarily operate as light node
    // Only validate headers, don't download full blocks
    is_degraded_mode_ = true;
    LOG_INFO(chrono_util::LogCategory::GENERAL,
             "Node in degraded mode: light sync only");
}
```

**2.1.3 Consensus Timeout Recovery**
- **Bestand:** `src/node/node_app.cpp`
- **Main loop aanpassen:**
```cpp
void NodeApp::run() {
    while (running_) {
        // ... existing logic ...
        
        // Detect stuck consensus
        auto now = std::chrono::steady_clock::now();
        if (now - last_consensus_progress_ > std::chrono::seconds(30)) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                     "Consensus appears stuck at height {}, round {}",
                     next_block_height_, bft_->get_current_round());
            
            // Force new round
            bft_->force_new_round("timeout_recovery");
            last_consensus_progress_ = now;
        }
    }
}
```

**Acceptance Criteria:**
- [ ] Storage failures trigger retry met exponential backoff
- [ ] Sync failures fallback naar alternative peers
- [ ] Degraded light sync mode bij geen viable sync peers
- [ ] Consensus stuck detection en recovery
- [ ] Alle kritieke operaties hebben fallback strategie
- [ ] Logging van alle degradation events
- [ ] Tests voor failure scenarios en recovery paths

---

### 2.2 Config Validation

**Status:** 🟠 Voorkomt runtime errors

#### Te Implementeren

**2.2.1 Config Validation Functie**
- **Bestand:** `src/node/config.hpp`
```cpp
struct Config {
    // ... existing fields ...
    
    // Validation
    void validate() const;
    
private:
    void validate_network_config() const;
    void validate_consensus_config() const;
    void validate_tokenomics_config() const;
    void validate_crypto_config() const;
};
```

- **Bestand:** `src/node/config.cpp`
```cpp
void Config::validate() const {
    validate_network_config();
    validate_consensus_config();
    validate_tokenomics_config();
    validate_crypto_config();
}

void Config::validate_network_config() const {
    // Port range validation
    if (listen_port < 1 || listen_port > 65535) {
        throw std::invalid_argument("listen_port must be between 1-65535, got: " + 
                                    std::to_string(listen_port));
    }
    
    if (rpc_port < 1 || rpc_port > 65535) {
        throw std::invalid_argument("rpc_port must be between 1-65535, got: " +
                                    std::to_string(rpc_port));
    }
    
    // Address validation
    if (listen_addr.empty()) {
        throw std::invalid_argument("listen_addr cannot be empty");
    }
    
    // Seeds validation
    for (const auto& seed : network_seeds) {
        if (seed.find(':') == std::string::npos) {
            throw std::invalid_argument("Invalid seed format (expected IP:PORT): " + seed);
        }
    }
}

void Config::validate_consensus_config() const {
    // Quorum validation
    if (bft_quorum < 0.5 || bft_quorum > 1.0) {
        throw std::invalid_argument("bft_quorum must be between 0.5-1.0, got: " +
                                    std::to_string(bft_quorum));
    }
    
    // Timeout validation
    if (bft_round_timeout_ms <= 0) {
        throw std::invalid_argument("bft_round_timeout_ms must be > 0");
    }
    
    // Validators validation
    if (validators.empty()) {
        throw std::invalid_argument("validators list cannot be empty");
    }
    
    // Validate each validator public key format
    for (const auto& val : validators) {
        if (val.size() < 64) {  // Minimum reasonable pubkey size
            throw std::invalid_argument("Invalid validator public key: " + val);
        }
    }
}

void Config::validate_tokenomics_config() const {
    if (max_total_supply == 0) {
        throw std::invalid_argument("max_total_supply must be > 0");
    }
    
    if (fee_burn_percentage > 100) {
        throw std::invalid_argument("fee_burn_percentage must be <= 100");
    }
    
    if (minting_enabled && initial_block_reward_nanos == 0) {
        LOG_WARN(chrono_util::LogCategory::GENERAL,
                 "minting_enabled but initial_block_reward_nanos is 0");
    }
}

void Config::validate_crypto_config() const {
    // HRP validation
    if (addr_hrp.empty() || addr_hrp.size() > 10) {
        throw std::invalid_argument("addr_hrp must be 1-10 characters");
    }
    
    // Algorithm validation
    std::vector<std::string> valid_algs = {"dilithium_2", "dilithium_3", "dilithium_5", "hmac"};
    if (std::find(valid_algs.begin(), valid_algs.end(), sign_alg) == valid_algs.end()) {
        throw std::invalid_argument("Unknown sign_alg: " + sign_alg);
    }
}
```

**2.2.2 Config Load met Validation**
- **Bestand:** `src/node/config.cpp`
- **Functie:** `Config::load()`
```cpp
Config Config::load(const std::string& file_path) {
    Config cfg;
    
    try {
        // ... existing TOML parsing ...
        
        // Validate after loading
        cfg.validate();
        
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL,
                  "Config validation failed: {}", e.what());
        throw;  // Re-throw to prevent startup with invalid config
    }
    
    return cfg;
}
```

**Acceptance Criteria:**
- [ ] Alle config parameters gevalideerd bij load
- [ ] Duidelijke error messages bij ongeldige waarden
- [ ] Port range checks (1-65535)
- [ ] Address format checks
- [ ] Quorum range checks (0.5-1.0)
- [ ] Validators list niet leeg
- [ ] Tokenomics parameters consistent
- [ ] Crypto algorithm supported
- [ ] Tests voor elke validatie regel

---

### 2.3 SecureSync Timeserver (Phase 1)

**Status:** 🟠 Verbetert tijdnauwkeurigheid en security

#### Design
- Software timeserver met Chrony
- Ondersteuning voor NTP en NTS (Network Time Security)
- Trusted NTS servers: time.cloudflare.com, nts.netnod.se, ptbtim1.ptb.de, 1.ntp.ubuntu.com

#### Te Implementeren

**2.3.1 ITimeSyncBackend Interface**
- **Nieuw bestand:** `src/consensus/ITimeSyncBackend.hpp`
```cpp
#pragma once
#include <optional>
#include <string>
#include <chrono>

namespace chrono_consensus {

struct TimeSample {
    std::chrono::milliseconds offset;  // Offset from local time
    double rtt_ms;                      // Round-trip time
    std::string source;                 // Server address
    bool authenticated;                 // True if NTS-secured
    uint64_t timestamp_ms;              // When sampled
};

class ITimeSyncBackend {
public:
    virtual ~ITimeSyncBackend() = default;
    
    virtual std::optional<TimeSample> query(const std::string& server) = 0;
    virtual bool supports_authentication() const = 0;
};

} // namespace chrono_consensus
```

**2.3.2 Chrony Backend Implementation**
- **Nieuw bestand:** `src/consensus/ChronyBackend.hpp`
```cpp
#pragma once
#include "ITimeSyncBackend.hpp"

namespace chrono_consensus {

class ChronyBackend : public ITimeSyncBackend {
public:
    ChronyBackend();
    ~ChronyBackend() override;
    
    std::optional<TimeSample> query(const std::string& server) override;
    bool supports_authentication() const override { return true; }
    
private:
    // Chrony client instance
    void* chrony_client_;  // Opaque pointer to chrony handle
    
    bool initialize_chrony();
    void cleanup_chrony();
};

} // namespace chrono_consensus
```

- **Nieuw bestand:** `src/consensus/ChronyBackend.cpp`
```cpp
#include "ChronyBackend.hpp"
#include "../util/log.hpp"
// Include Chrony C API headers
// #include <chrony/client.h>

namespace chrono_consensus {

ChronyBackend::ChronyBackend() : chrony_client_(nullptr) {
    if (!initialize_chrony()) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                 "Failed to initialize Chrony backend, falling back to basic NTP");
    }
}

std::optional<TimeSample> ChronyBackend::query(const std::string& server) {
    // TODO: Implement Chrony NTS query
    // For now, placeholder implementation
    
    // chrony_query_result result = chrony_query(chrony_client_, server.c_str());
    // if (!result.success) {
    //     return std::nullopt;
    // }
    
    TimeSample sample;
    sample.source = server;
    sample.authenticated = true;  // NTS provides authentication
    // sample.offset = std::chrono::milliseconds(result.offset_ms);
    // sample.rtt_ms = result.rtt_ms;
    sample.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    return sample;
}

bool ChronyBackend::initialize_chrony() {
    // TODO: Initialize Chrony client
    // chrony_client_ = chrony_client_create();
    // return chrony_client_ != nullptr;
    return false;  // Placeholder
}

void ChronyBackend::cleanup_chrony() {
    if (chrony_client_) {
        // chrony_client_destroy(chrony_client_);
        chrony_client_ = nullptr;
    }
}

ChronyBackend::~ChronyBackend() {
    cleanup_chrony();
}

} // namespace chrono_consensus
```

**2.3.3 Config Updates**
- **Bestand:** `config/default.toml`
- **Toevoegen:**
```toml
[secure_time]
enable_nts = true
max_skew_ms = 1000  # Reject samples with > 1s skew
query_interval_ms = 10000
min_nts_quorum = 2  # Require at least 2 authenticated sources

# NTS-enabled servers (prioritized)
nts_servers = [
    "time.cloudflare.com",
    "nts.netnod.se",
    "ptbtim1.ptb.de",
    "1.ntp.ubuntu.com"
]

# Fallback NTP servers (unauthenticated)
[external_time_sources]
ntp_servers = ["pool.ntp.org", "time.google.com"]
ntp_query_interval_ms = 5000
```

**2.3.4 ExternalTimeSourceManager Update**
- **Bestand:** `src/consensus/external_time_source_manager.hpp`
```cpp
#include "ITimeSyncBackend.hpp"

class ExternalTimeSourceManager {
public:
    ExternalTimeSourceManager(
        const std::vector<std::string>& ntp_servers,
        long query_interval_ms,
        std::shared_ptr<ITimeSyncBackend> backend = nullptr  // NEW
    );
    
    void set_backend(std::shared_ptr<ITimeSyncBackend> backend);
    
private:
    std::shared_ptr<ITimeSyncBackend> backend_;
    std::vector<TimeSample> recent_samples_;
    
    void query_with_backend();
    bool validate_sample(const TimeSample& sample) const;
};
```

- **Bestand:** `src/consensus/external_time_source_manager.cpp`
```cpp
void ExternalTimeSourceManager::query_with_backend() {
    if (!backend_) {
        // Fallback to existing NTP logic
        query_ntp_servers();
        return;
    }
    
    for (const auto& server : ntp_servers_) {
        auto sample = backend_->query(server);
        
        if (!sample) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                     "Failed to query time from {}", server);
            continue;
        }
        
        if (!validate_sample(*sample)) {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS,
                     "Invalid time sample from {} (skew too large or unauthenticated)",
                     server);
            continue;
        }
        
        recent_samples_.push_back(*sample);
        
        // Pass to PoTAggregator
        if (callback_) {
            TimeMeasurement measurement;
            measurement.timestamp_ms = sample->timestamp_ms;
            measurement.offset_ms = sample->offset.count();
            measurement.source = sample->source;
            measurement.authenticated = sample->authenticated;
            callback_(measurement);
        }
    }
}

bool ExternalTimeSourceManager::validate_sample(const TimeSample& sample) const {
    // Check skew
    if (std::abs(sample.offset.count()) > max_skew_ms_) {
        return false;
    }
    
    // If NTS required, check authentication
    if (require_nts_ && !sample.authenticated) {
        return false;
    }
    
    return true;
}
```

**Acceptance Criteria:**
- [ ] ITimeSyncBackend interface gedefinieerd
- [ ] ChronyBackend placeholder implementatie
- [ ] Config heeft [secure_time] sectie met NTS servers
- [ ] ExternalTimeSourceManager accepteert backend injection
- [ ] Validation van authenticated samples
- [ ] Logging van NTS vs NTP samples
- [ ] Fallback naar basic NTP als NTS fails
- [ ] Tests voor time sample validation
- [ ] Documentation voor Chrony installation

---

## 🔵 PRIORITEIT 3: MEDIUM (Verbetering & Robuustheid)

### 3.1 Node Politics & Governance

**Status:** 🔵 Feature development (zie TODO.md sectie 1-8)

Implementatie volledig beschreven in TODO.md onder "Node Politics & Governance (Plan)". Kernpunten:

**Te Implementeren (Samenvatting):**
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
- **Bestand:** `src/storage/file_kv.cpp`
- **Probleem:** Volledige file rewrite bij elke put()
- **Oplossing:**
```cpp
// Add caching layer
class FileKv {
private:
    std::unordered_map<std::string, std::string> cache_;
    bool dirty_ = false;
    
    // Batch writes
    void flush_if_needed() {
        if (dirty_ && cache_.size() > FLUSH_THRESHOLD) {
            flush_to_disk();
        }
    }
    
    void flush_to_disk() {
        // Write all cached entries atomically
        // Use temp file + rename for atomicity
    }
};
```

#### 3.2.2 PoT Aggregator Optimization
- **Bestand:** `src/consensus/pot_aggregator.cpp`
- **Functie:** `aggregate()`
- **Probleem:** O(n log n) sort voor median
- **Oplossing:** Gebruik partial_sort of nth_element
```cpp
double PoTAggregator::calculate_median(std::vector<double>& values) {
    size_t n = values.size();
    if (n == 0) return 0.0;
    
    // Use nth_element instead of full sort (O(n) average)
    size_t mid = n / 2;
    std::nth_element(values.begin(), values.begin() + mid, values.end());
    
    if (n % 2 == 0) {
        double mid_val = values[mid];
        std::nth_element(values.begin(), values.begin() + mid - 1, values.end());
        return (mid_val + values[mid - 1]) / 2.0;
    } else {
        return values[mid];
    }
}
```

---

### 3.3 P2P Robuustheid

**Status:** 🔵 Verbeteringen voor productie reliability

#### 3.3.1 Reconnect Mechanisme
- **Bestand:** `src/p2p/p2p_client.cpp`
- **Toevoegen:**
```cpp
class P2PClient {
private:
    bool auto_reconnect_ = true;
    int reconnect_delay_ms_ = 1000;
    int max_reconnect_attempts_ = 5;
    
    void handle_disconnect() {
        if (!auto_reconnect_) return;
        
        for (int attempt = 1; attempt <= max_reconnect_attempts_; ++attempt) {
            LOG_INFO(chrono_util::LogCategory::P2P,
                     "Reconnect attempt {} / {}", attempt, max_reconnect_attempts_);
            
            std::this_thread::sleep_for(std::chrono::milliseconds(reconnect_delay_ms_));
            
            if (connect(server_addr_, server_port_)) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Reconnected successfully");
                return;
            }
            
            reconnect_delay_ms_ *= 2;  // Exponential backoff
        }
        
        LOG_ERROR(chrono_util::LogCategory::P2P,
                  "Failed to reconnect after {} attempts", max_reconnect_attempts_);
    }
};
```

#### 3.3.2 Message Fragmentatie
- **Bestand:** `proto/p2p_messages.proto`
- **Toevoegen:**
```protobuf
message FragmentedMessage {
    string message_id = 1;      // UUID for reassembly
    uint32 fragment_index = 2;  // 0-based index
    uint32 total_fragments = 3; // Total number of fragments
    bytes fragment_data = 4;    // Actual data chunk
}
```

- **Bestand:** `src/p2p/message_fragmenter.hpp`
```cpp
class MessageFragmenter {
public:
    static constexpr size_t MAX_FRAGMENT_SIZE = 65536;  // 64KB
    
    std::vector<FragmentedMessage> fragment(const Bytes& message);
    std::optional<Bytes> reassemble(const std::vector<FragmentedMessage>& fragments);
    
private:
    std::unordered_map<std::string, std::vector<FragmentedMessage>> pending_;
};
```

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
6. LevelDB Storage Backend
7. Peer Discovery Mechanisme
8. Peer Store Persistence
9. Bootstrap Node Setup

### Phase 3: Recovery & Robustness (Week 5)
10. Graceful Degradation
11. Error Recovery Paths
12. Consensus Stuck Detection

### Phase 4: Wallet & UX (Week 6-7)
13. Wallet CLI Commands (balance, send, history)
14. RPC Endpoints voor Wallet
15. Key Import/Export

### Phase 5: Governance (Week 8-10)
16. Node Identity & Registry
17. Stake & Slashing Logic
18. Approval Flow Implementation

### Phase 6: Advanced Features (Week 11-12)
19. SecureSync Timeserver (Chrony/NTS)
20. Performance Optimalisaties
21. P2P Robuustheid (reconnect, fragmentatie)

### Phase 7: Polish (Week 13-14)
22. Code Cleanup
23. Documentation
24. Testing & Bug Fixes

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
