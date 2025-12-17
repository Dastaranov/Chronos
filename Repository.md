chronos/                                 # Hoofdmap van het Chronos project
├─ CMakeLists.txt                         # CMake-configuratiebestand voor het bouwen van het project
├─ README.md                              # Algemene informatie over het project
├─ GEMINI.md                              # Gemini CLI specifieke documentatie
├─ FAQ.md                                 # Veelgestelde vragen en antwoorden over het project
├─ Repository.md                          # Dit bestand, een overzicht van de repository structuur
├─ .vscode/                               # Instellingen voor Visual Studio Code
├─ config/                                # Map voor configuratiebestanden
│  ├─ default.toml                        # Standaard TOML-configuratiebestand
│  └─ ...
├─ proto/                                 # Protobuf definitiebestanden
│  ├─ bft_messages.proto                  # Protobuf-definities voor BFT-berichten
│  └─ p2p_messages.proto                  # Protobuf-definities voor P2P-berichten
├─ external/                              # Externe afhankelijkheden die direct in de tree zijn opgenomen
│  └─ blake3/                             # BLAKE3 hash-implementatie (vendored)
├─ src/                                   # Broncode van het project
│  ├─ main.cpp                            # Hoofdbestand van de Chronos node applicatie
│  ├─ address/                            # Code voor het aanmaken, valideren en beheren van Chronos-adressen
│  ├─ consensus/                          # Code voor de consensusalgoritmes (BFT en PoT)
│  ├─ crypto/                             # Cryptografische functies (hashing, signing)
│  │  ├─ signer_dilithium.cpp/hpp         # Dilithium post-quantum signer implementatie
│  │  ├─ key_manager.cpp/hpp              # 🆕 Secure key storage en Base58Check encoding
│  │  ├─ blake3.cpp/hpp                   # BLAKE3 hashing
│  │  └─ ...
│  ├─ ledger/                             # Logica voor het grootboek, blokken en transacties
│  ├─ node/                               # Kernapplicatielogica van de node (config, app-loop)
│  ├─ p2p/                                # Peer-to-peer netwerklogica (gossip, transport, peer management)
│  ├─ rpc/                                # JSON-RPC server voor externe communicatie
│  ├─ storage/                            # Opslaglogica (key-value stores, blockchain-opslag)
│  ├─ util/                               # Hulpprogramma's (logging, byte-manipulatie, console display)
│  └─ wallet/
│     └─ cli/                             # 🆕 Verbeterde wallet CLI met key management
├─ tests/                                 # Map voor unit- en integratietests
│  ├─ test_address.cpp                    # Testen voor adresfunctionaliteit
│  ├─ test_bft.cpp                        # Testen voor BFT-consensus
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
