chronos/                                 # Hoofdmap van het Chronos project
├─ CMakeLists.txt                         # CMake-configuratiebestand voor het bouwen van het project
├─ README.md                              # Algemene informatie over het project
├─ USER_MANUAL.md                         # 🆕 Gebruikershandleiding (Wallet, Node, Staking)
├─ DEVELOPER_GUIDE.md                     # 🆕 Gids voor ontwikkelaars (Code style, Architecture)
├─ IMPLEMENTATION_ROADMAP.md              # Roadmap en status van implementatie
├─ SECURE_TIME_DESIGN.md                  # 🆕 Design document voor Proof-of-Time en Chrony
├─ UPGRADABILITY_STRATEGY.md              # 🆕 Strategie voor toekomstige upgrades
├─ GEMINI.md                              # Gemini CLI specifieke documentatie
├─ FAQ.md                                 # Veelgestelde vragen en antwoorden over het project
├─ Repository.md                          # Dit bestand, een overzicht van de repository structuur
├─ quickstart.sh                          # 🆕 Script voor snelle installatie en setup
├─ .vscode/                               # Instellingen voor Visual Studio Code
├─ config/                                # Map voor configuratiebestanden
│  ├─ default.toml                        # Standaard TOML-configuratiebestand
│  └─ ...
├─ proto/                                 # Protobuf definitiebestanden
│  ├─ bft_messages.proto                  # Protobuf-definities voor BFT-berichten
│  ├─ p2p_messages.proto                  # Protobuf-definities voor P2P-berichten
│  └─ ledger.proto                        # 🆕 Protobuf-definities voor opslag (Blocks/Tx)
├─ external/                              # Externe afhankelijkheden die direct in de tree zijn opgenomen
│  └─ blake3/                             # BLAKE3 hash-implementatie (vendored)
├─ src/                                   # Broncode van het project
│  ├─ main.cpp                            # Hoofdbestand van de Chronos node applicatie
│  ├─ address/                            # Code voor het aanmaken, valideren en beheren van Chronos-adressen
│  ├─ consensus/                          # Code voor de consensusalgoritmes (BFT en PoT)
│  │  ├─ bft.cpp/hpp                      # BFT Gadget implementatie
│  │  ├─ pot_aggregator.cpp/hpp           # Proof-of-Time aggregatie
│  │  ├─ external_time_source_manager.cpp # Beheer van externe tijdsbronnen
│  │  ├─ ChronyBackend.cpp/hpp            # 🆕 Chrony/NTS integratie
│  │  ├─ AtomicClockBackend.cpp/hpp       # 🆕 Ondersteuning voor atoomklokken
│  │  └─ QuantumClockBackend.cpp/hpp      # 🆕 Ondersteuning voor quantumklokken
│  ├─ crypto/                             # Cryptografische functies (hashing, signing)
│  │  ├─ signer_dilithium.cpp/hpp         # Dilithium post-quantum signer implementatie
│  │  ├─ kyber_crypto.cpp/hpp             # 🆕 Kyber key encapsulation voor P2P
│  │  ├─ aes_crypto.cpp/hpp               # 🆕 AES-256-GCM encryptie
│  │  ├─ key_manager.cpp/hpp              # Secure key storage en Base58Check encoding
│  │  └─ blake3.cpp/hpp                   # BLAKE3 hashing
│  ├─ ledger/                             # Logica voor het grootboek, blokken en transacties
│  │  ├─ node_registry.cpp/hpp            # 🆕 Beheer van validators en staking
│  │  └─ ...
│  ├─ node/                               # Kernapplicatielogica van de node (config, app-loop)
│  ├─ p2p/                                # Peer-to-peer netwerklogica
│  │  ├─ discovery_manager.cpp/hpp        # 🆕 Peer discovery en bootstrap
│  │  ├─ peer_store.cpp/hpp               # 🆕 Persistente opslag van peers
│  │  └─ ...
│  ├─ rpc/                                # JSON-RPC server voor externe communicatie
│  ├─ storage/                            # Opslaglogica (key-value stores, blockchain-opslag)
│  │  ├─ LevelDBBlockchainStorage.cpp/hpp # 🆕 LevelDB implementatie met Protobuf
│  │  └─ ...
│  ├─ tools/                              # 🆕 CLI tools
│  │  ├─ node_cli.cpp                     # Admin CLI voor node beheer
│  │  ├─ genesis_tool.cpp                 # Tool voor genesis block creatie
│  │  └─ network_viz.cpp                  # 🆕 Tool voor netwerk visualisatie
│  ├─ util/                               # Hulpprogramma's (logging, byte-manipulatie, console display)
│  └─ wallet/
│     └─ cli/                             # Wallet CLI broncode
│        ├─ main.cpp                      # Entry point
│        └─ node_connector.cpp/hpp        # 🆕 Multi-node connectiviteit en failover
├─ tests/                                 # Map voor unit- en integratietests
│  ├─ test_address.cpp                    # Testen voor adresfunctionaliteit
│  ├─ test_bft.cpp                        # Testen voor BFT-consensus
│  ├─ test_robust_integration.cpp         # 🆕 Uitgebreide integratietest
│  └─ ...
└─ wallet/                                # Map voor de wallet-applicatie

## Key Management System (NEW)

**src/crypto/key_manager.hpp/cpp:**
- Secure file-based key storage (`~/.chronos/keys/`)
- Base58Check encoding/decoding for public keys
- Key listing and validation
- File-based permissions management (owner-readable/writable only)

**Advantages:**
✅ Private keys never exposed in plaintext config
✅ Base58Check public keys ~60% shorter than hex
✅ Error detection prevents typos
✅ Multiple validators easily manageable

**Usage:**
```bash
wallet_cli generate-keys validator-1           # Create and store key
wallet_cli list-keys                           # Show all available keys
wallet_cli show-public validator-1             # Display public key for config
```

**Config Integration:**
```toml
[crypto]
private_key_id = "validator-1"  # Reference to key file, not hex string

[consensus]
validators = ["cqc1zg4ptpfysee0..."]  # Base58Check public key (much shorter)
```
