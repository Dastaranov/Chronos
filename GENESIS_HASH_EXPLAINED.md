# Genesis Hash: Hoe Nodes Synchroon Blijven

## Nederlandse Uitleg

### Wat is de Genesis Hash?

De genesis hash is een unieke cryptografische vingerafdruk van het eerste blok (genesis block) in de Chronos blockchain. Deze hash zorgt ervoor dat alle nodes in het netwerk vanaf **exact hetzelfde startpunt** beginnen. Als zelfs één bit verschilt in het genesis block, zal de hash volledig anders zijn, waardoor nodes elkaar detecteren als incompatibel.

### Waarom is de Genesis Hash Belangrijk?

De genesis hash fungeert als een **netwerkidentificatie** en **consistentiecontrole**:

1. **Netwerkidentificatie**: Nodes met dezelfde genesis hash behoren tot hetzelfde netwerk
2. **Fork-preventie**: Verhindert dat nodes per ongeluk verschillende chains volgen
3. **Reproduceerbare Initialisatie**: Garandeert dat iedereen met dezelfde configuratie dezelfde blockchain start
4. **Detectie van Configuratiefouten**: Als een node een verkeerde genesis hash heeft, wordt dit onmiddellijk gedetecteerd

### Hoe Werkt het Genesis Block?

Het genesis block in Chronos heeft een specifieke structuur die **deterministisch** is - dat wil zeggen, met dezelfde input krijg je altijd dezelfde output.

#### Genesis Block Structuur

Het genesis block bevat de volgende velden:

```cpp
// Genesis block velden
prev_block_hash:           32 bytes van nullen (0x00000000...)
height:                    0 (het eerste blok)
consensus_time:            Configureerbare timestamp (in milliseconden)
timestamp:                 Unix timestamp (in seconden)
transactions:              Leeg (geen transacties in genesis)
transactions_merkle_root:  Hash van lege transactielijst
```

#### Genesis Hash Berekening

De genesis hash wordt berekend door de volgende stappen:

```
1. Serialiseer alle header-velden:
   - prev_block_hash (32 bytes)
   - timestamp (8 bytes, uint64_t)
   - transactions_merkle_root (32 bytes)
   - height (8 bytes, uint64_t) 
   - consensus_time (8 bytes, uint64_t)

2. Concateneer alle bytes in vaste volgorde

3. Bereken BLAKE3 hash (256-bit / 32 bytes):
   genesis_hash = BLAKE3(serialized_header)
```

**Belangrijk**: De hash is volledig deterministisch. Als twee nodes dezelfde configuratie gebruiken, krijgen ze **exact dezelfde genesis hash**.

### Implementatie in Chronos

#### Locatie in Code

Het genesis block wordt aangemaakt in `src/node/node_app.cpp` in de `run()` methode:

```cpp
// Creëer genesis block als de blockchain leeg is
Bytes zero_hash(32, 0);  // Previous hash = allemaal nullen
uint64_t genesis_consensus_time = cfg_.genesis_consensus_time;

Block genesis_block(zero_hash, 0, genesis_consensus_time, {});

// Valideer tegen verwachte hash
if (!cfg_.genesis_expected_hash.empty()) {
    Bytes expected_hash = hex_to_bytes(cfg_.genesis_expected_hash);
    Bytes actual_hash = genesis_block.get_header_hash();
    
    if (expected_hash != actual_hash) {
        // FOUT! Deze node heeft een andere genesis configuratie
        throw std::runtime_error("Genesis block hash mismatch!");
    }
}
```

#### Hash Berekening

De hash wordt berekend in `src/ledger/block.cpp`:

```cpp
Bytes Block::get_header_hash() const {
    Bytes header_data;
    
    // 1. Voeg prev_block_hash toe (32 bytes)
    header_data.insert(header_data.end(), 
                      prev_block_hash.begin(), 
                      prev_block_hash.end());
    
    // 2. Voeg timestamp toe (8 bytes, little-endian)
    Bytes timestamp_bytes(sizeof(timestamp));
    std::memcpy(timestamp_bytes.data(), &timestamp, sizeof(timestamp));
    header_data.insert(header_data.end(), 
                      timestamp_bytes.begin(), 
                      timestamp_bytes.end());
    
    // 3. Voeg merkle root toe (32 bytes)
    header_data.insert(header_data.end(), 
                      transactions_merkle_root.begin(), 
                      transactions_merkle_root.end());
    
    // 4. Voeg height toe (8 bytes, little-endian)
    Bytes height_bytes(sizeof(height));
    std::memcpy(height_bytes.data(), &height, sizeof(height));
    header_data.insert(header_data.end(), 
                      height_bytes.begin(), 
                      height_bytes.end());
    
    // 5. Voeg consensus_time toe (8 bytes, little-endian)
    Bytes consensus_time_bytes(sizeof(consensus_time));
    std::memcpy(consensus_time_bytes.data(), &consensus_time, sizeof(consensus_time));
    header_data.insert(header_data.end(), 
                      consensus_time_bytes.begin(), 
                      consensus_time_bytes.end());
    
    // 6. Bereken BLAKE3 hash
    return blake3(header_data);
}
```

### Configuratie: Genesis Hash Instellen

In `config/default.toml` kun je de verwachte genesis hash configureren:

```toml
[genesis]
# Consensus time voor het genesis block (milliseconden sinds epoch)
# Gebruik een vaste waarde voor reproduceerbare genesis hash
consensus_time = 1704067200000  # 1 januari 2024 00:00:00 UTC

# Verwachte hash van het genesis block (hex-encoded)
# Alle nodes in het netwerk MOETEN deze hash hebben
expected_hash = "a1b2c3d4e5f6..."  # Vervang met je actuele genesis hash

# Maximaal saldo per account (overflow bescherming)
max_supply_per_account = 1000000000000000

# Genesis allocaties: initiële balansen
[genesis.allocations]
"cqc1q..." = 1000000000  # Eerste account krijgt 1 miljard nanos
"cqc1z..." = 500000000   # Tweede account krijgt 500 miljoen nanos
```

### Genesis Hash Genereren: Stap-voor-Stap

#### Stap 1: Configureer Genesis Parameters

Bewerk `config/default.toml`:

```toml
[genesis]
consensus_time = 1704067200000  # Vaste timestamp
expected_hash = ""               # Laat leeg voor eerste run

[genesis.allocations]
"cqc1validatoraddr123..." = 1000000000
```

#### Stap 2: Start de Node (Eerste Keer)

```bash
./chronos_node --config config/default.toml
```

De node zal:
1. Detecteren dat er geen blockchain bestaat
2. Een genesis block aanmaken
3. De hash loggen in de console en logfile

Zoek in de output naar:

```
[CONSENSUS] Genesis block created with hash: a1b2c3d4e5f6789...
```

#### Stap 3: Kopieer de Genesis Hash

Kopieer de volledige hex string van de genesis hash.

#### Stap 4: Update Configuratie

Voeg de hash toe aan `config/default.toml`:

```toml
[genesis]
consensus_time = 1704067200000
expected_hash = "a1b2c3d4e5f6789..."  # Geplakte hash van stap 3
```

#### Stap 5: Distribueer Configuratie

**BELANGRIJK**: Alle nodes in het netwerk moeten **exact dezelfde** `config/default.toml` gebruiken:
- Zelfde `genesis.consensus_time`
- Zelfde `genesis.allocations` (volgorde maakt niet uit, maar adressen en balansen moeten identiek zijn)
- Zelfde `genesis.expected_hash`

### Hoe Nodes Elkaar Valideren

Wanneer een node start met een niet-lege blockchain:

```cpp
// Laad bestaande blockchain
auto height_data = blockchain_storage_->getMetadata(NEXT_BLOCK_HEIGHT_KEY);
auto hash_data = blockchain_storage_->getMetadata(LAST_BLOCK_HASH_KEY);

// Gebruik geladen waarden
last_block_hash_ = *hash_data;
next_block_height_ = loaded_height;
```

Wanneer nodes verbinden met elkaar:
1. Ze wisselen block headers uit via P2P gossip
2. Als een node een block ontvangt met een `height = 0`, controleert het de hash
3. Als de genesis hash niet matcht, wordt de peer als **incompatibel** beschouwd

### Praktisch Voorbeeld

#### Scenario: Netwerk met 3 Validators

**Node A, B, en C willen hetzelfde netwerk starten.**

1. **Coordinator (bijv. Node A)** maakt eerste configuratie:
   ```toml
   [genesis]
   consensus_time = 1704067200000
   expected_hash = ""
   
   [genesis.allocations]
   "cqc1validator_a..." = 1000000000
   "cqc1validator_b..." = 1000000000
   "cqc1validator_c..." = 1000000000
   ```

2. **Node A start** en logt:
   ```
   Genesis block created with hash: 4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c6e5d8a1f4b7c2e9d6a3f8b1c4e7d2a5f
   ```

3. **Coordinator distribueert** de volledige configuratie (met genesis hash) naar Node B en C

4. **Alle nodes gebruiken exact dezelfde config**:
   ```toml
   [genesis]
   consensus_time = 1704067200000
   expected_hash = "4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c6e5d8a1f4b7c2e9d6a3f8b1c4e7d2a5f"
   ```

5. **Nodes B en C starten** en valideren:
   ```
   [CONSENSUS] Genesis block hash validated successfully
   ```

#### Wat Gebeurt er bij een Fout?

Als Node C per ongeluk een verkeerde configuratie gebruikt:

```toml
# FOUT: Verkeerde consensus_time!
consensus_time = 1704067200001  # +1 milliseconde verschil
```

Dan zal Node C crashen bij start:

```
[ERROR] Genesis block hash mismatch!
Expected: 4f8b2a1c9e3d7f6a5b8c2e1d4a7f9b3c6e5d8a1f4b7c2e9d6a3f8b1c4e7d2a5f
Got:      9a3c5e7f1b4d6a8c2e0f4b7d9a1c3e5f7b9d1f3a5c7e9b1d3f5a7c9e1b3d5f7a

Genesis block hash mismatch!
```

Dit voorkomt dat Node C per ongeluk een **fork** creëert of het verkeerde netwerk joined.

### Belangrijke Overwegingen

#### 1. Determinisme is Cruciaal

De genesis hash is alleen betrouwbaar als de berekening **deterministisch** is:

- ✅ **Gebruik vaste consensus_time**: Niet de systeemtijd gebruiken
- ✅ **Vaste byte volgorde**: Altijd dezelfde serialisatie
- ✅ **Identieke allocaties**: Alle adressen en balansen moeten kloppen
- ⚠️ **Let op timestamp**: Het `timestamp` veld wordt automatisch gezet bij constructie en kan variëren

**Huidige Limitatie**: Het `timestamp` veld wordt gezet via `std::chrono::system_clock::now()`, wat **niet-deterministisch** is. Voor een volledig deterministisch genesis block moet dit ook geconfigureerd worden.

#### 2. Genesis Allocaties Beïnvloeden de Hash

Het genesis block zelf bevat **geen transacties**, maar de configuratie van `genesis.allocations` moet **identiek** zijn op alle nodes omdat:

1. Allocaties worden toegepast op de State
2. State beïnvloedt de werking van het netwerk
3. Hoewel allocaties niet in het block zitten, is consistency cruciaal

**Best Practice**: Distribueer altijd de **volledige** configuratiefile, niet alleen de hash.

#### 3. Wijzigingen Vereisen Nieuwe Genesis

Als je de genesis configuratie wijzigt:
- De genesis hash verandert
- Dit creëert een **nieuw netwerk**
- Oude nodes kunnen niet meer verbinden

Dit is **gewenst gedrag** voor:
- Testnet vs Mainnet onderscheid
- Verschillende fork/versies van de chain
- Geïsoleerde ontwikkelomgevingen

### Technische Details

#### BLAKE3 Hash Functie

Chronos gebruikt BLAKE3 voor alle cryptografische hashing:

- **Output**: 256-bit (32 bytes)
- **Performance**: Sneller dan SHA-256, SHA-3
- **Security**: Post-quantum resistant tegen hash collision attacks
- **Determinisme**: Gegarandeerd deterministisch voor zelfde input

#### Byte Ordering (Endianness)

De huidige implementatie gebruikt **native endianness** (meestal little-endian op moderne systemen):

```cpp
std::memcpy(timestamp_bytes.data(), &timestamp, sizeof(timestamp));
```

**Toekomstige Verbetering**: Voor maximale portabiliteit zou expliciet little-endian encoding gebruikt moeten worden (zie `src/util/codec.hpp`).

### Tests

De genesis hash functionaliteit wordt getest in `tests/test_genesis.cpp`:

```cpp
TEST_CASE(GenesisHashValidation, "Genesis Hash Validation") {
    // Maak twee identieke genesis blocks
    Bytes zero_hash(32, 0);
    
    Block genesis1;
    genesis1.prev_block_hash = zero_hash;
    genesis1.height = 0;
    genesis1.consensus_time = 1704067200000;
    genesis1.timestamp = 1704067200;
    genesis1.transactions = {};
    genesis1.calculate_merkle_root();
    
    Block genesis2;
    genesis2.prev_block_hash = zero_hash;
    genesis2.height = 0;
    genesis2.consensus_time = 1704067200000;
    genesis2.timestamp = 1704067200;
    genesis2.transactions = {};
    genesis2.calculate_merkle_root();
    
    Bytes hash1 = genesis1.get_header_hash();
    Bytes hash2 = genesis2.get_header_hash();
    
    // Hashes moeten identiek zijn
    ASSERT_TRUE(hash1 == hash2);
}
```

### Veelgestelde Vragen

**Q: Kan ik de genesis hash handmatig berekenen?**

A: Ja, als je de exacte veldwaarden kent. Gebruik de formule:
```
genesis_hash = BLAKE3(
    zero_bytes(32) ||           // prev_block_hash
    uint64_le(timestamp) ||     // timestamp in seconden
    merkle_root(empty_txs) ||   // merkle root van lege tx lijst
    uint64_le(0) ||             // height = 0
    uint64_le(consensus_time)   // consensus_time in milliseconden
)
```

**Q: Wat als mijn node een verkeerde genesis hash detecteert?**

A: De node zal crashen met een error message. Dit is **gewenst gedrag** - het voorkomt dat je node per ongeluk een verkeerd netwerk joined. Controleer je configuratie en zorg dat alle velden kloppen.

**Q: Kunnen twee verschillende netwerken dezelfde genesis hash hebben?**

A: Technisch mogelijk maar **extreem onwaarschijnlijk**. BLAKE3 is een cryptografische hash functie waarbij zelfs een 1-bit verschil in input resulteert in een volledig verschillende 256-bit output. De kans op een collision is astronomisch klein (2^-256).

**Q: Moet ik de genesis hash delen met andere node operators?**

A: Ja, deel de **volledige configuratiefile** (`config/default.toml`) met alle node operators die je netwerk willen joinen. De genesis hash alleen is niet voldoende - ze hebben ook de allocaties en andere parameters nodig.

**Q: Wat gebeurt er als ik per ongeluk de verkeerde genesis configuratie gebruik?**

A: Je node zal een **eigen, geïsoleerde blockchain** starten die incompatibel is met het hoofdnetwerk. Andere nodes zullen je blocks rejecten en je kunt niet synchroniseren. Verwijder je `data/` directory en herstart met de correcte configuratie.

### Samenvatting

De genesis hash in Chronos is een **fundamentele security feature** die garandeert dat:

1. ✅ Alle nodes hetzelfde startpunt hebben
2. ✅ Configuratiefouten direct gedetecteerd worden  
3. ✅ Netwerkisolatie mogelijk is (testnet vs mainnet)
4. ✅ Geen per ongeluk forks kunnen ontstaan

**Kritieke Stappen voor een Nieuw Netwerk:**
1. Configureer genesis parameters (consensus_time, allocations)
2. Start eerste node en noteer de genesis hash
3. Distribueer volledige configuratie naar alle validators
4. Alle nodes valideren automatisch bij start

**Remember**: De genesis hash is een *cryptografische garantie* dat alle nodes dezelfde spelregels volgen vanaf het allereerste block.
