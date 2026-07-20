# Chronos Blockchain Node

[![Build and Test](https://github.com/Dastaranov/Chronos/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/Dastaranov/Chronos/actions/workflows/build-and-test.yml)
[![Memory Check](https://github.com/Dastaranov/Chronos/actions/workflows/memory-check.yml/badge.svg)](https://github.com/Dastaranov/Chronos/actions/workflows/memory-check.yml)
[![CodeQL Analysis](https://github.com/Dastaranov/Chronos/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/Dastaranov/Chronos/actions/workflows/codeql-analysis.yml)

A C++20 blockchain node with Byzantine Fault Tolerant (BFT) consensus, Proof-of-Time (PoT) aggregation, and post-quantum cryptography (CRYSTALS-Dilithium via liboqs).

## Features

| Feature | Status | Details |
|---------|--------|---------|
| BFT Consensus | ✅ Working | Prevote → Precommit → Finalization pipeline |
| Proof-of-Time | ✅ Working | Median/MAD filtering of NTP measurements |
| Post-Quantum Signing | ✅ Working | CRYSTALS-Dilithium-3 (liboqs) |
| Post-Quantum P2P | ✅ Working | Kyber KEM key exchange + AES-GCM encryption |
| LevelDB Storage | ✅ Working | Full nodes use LevelDB with Protobuf serialization |
| Peer Discovery | ✅ Working | Gossip-based peer exchange |
| Wallet CLI | ✅ Working | Key generation, balance, send, history |
| Time Sync | ✅ Working | NTP + Chrony/NTS backends |
| Docker Deployment | ✅ Working | Multi-stage build, see README-DOCKER.md |
| Atomic Clock | 🔧 Planned | Integration point ready (`AtomicClockBackend`) |
| Quantum Clock | 🔧 Planned | Integration point ready (`QuantumClockBackend`) |

## Architecture

```
src/
├── consensus/     BFT state machine, PoT aggregator, time sync backends
├── crypto/        Dilithium signer, Kyber KEM, AES-GCM, BLAKE3 hasher
├── ledger/        Block, Transaction, State, NodeRegistry
├── node/          NodeApp orchestrator, Config loader
├── p2p/           Gossip, P2P server/client, peer discovery
├── rpc/           JSON-RPC server (httplib)
├── storage/       LevelDB, Disk, Memory backends (IBlockchainStorage)
├── util/          Logging, bytes, codec, retry policy
└── wallet/cli/    wallet_cli binary
```

**Data flow:**
1. Transactions arrive via RPC → validated → mempool
2. BFT leader proposes block → Prevote → Precommit on 2/3+ quorum → finalized
3. ExternalTimeSourceManager queries NTP/Chrony → PoTAggregator aggregates → `consensus_time` in block

## Time Synchronization Tiers

Validators must maintain at least **Tier 4** status:

| Tier | Backend | Source |
|------|---------|--------|
| 1 | Quantum Clock | Future quantum hardware integration |
| 2 | Atomic Clock | Future atomic clock hardware integration |
| 3 | GNSS/GPS | Direct GPS receiver |
| 4 | Chrony + NTS | Authenticated NTS — **minimum for validators** |
| 5 | NTP | Standard NTP — fallback only |

## Building

### Prerequisites

```bash
# Ubuntu 22.04
sudo apt install build-essential cmake git pkg-config \
    libssl-dev libsnappy-dev protobuf-compiler libprotobuf-dev
```

**liboqs** (post-quantum crypto) must be installed separately:
```bash
git clone --depth=1 --branch 0.10.1 https://github.com/open-quantum-safe/liboqs.git
cd liboqs && mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DOQS_BUILD_ONLY_LIB=ON -DCMAKE_INSTALL_PREFIX=/usr/local
make -j4 && sudo make install
```

### Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

**CMake variables** (override defaults):
```bash
cmake .. -DPROTOBUF_LOCAL_ROOT=/path/to/protobuf \
         -DLEVELDB_LOCAL_ROOT=/path/to/leveldb \
         -DOQS_ROOT=/usr/local \
         -DCHRONOS_USE_OQS=ON
```

### Run tests

```bash
cd build && ctest --output-on-failure
```

## Quick Start

### 1. Generate validator key

```bash
./build/wallet_cli generate-keys validator-1
./build/wallet_cli show-public validator-1   # copy this pubkey
```

### 2. Configure

Edit `config/default.toml`:
```toml
[crypto]
private_key_id = "validator-1"
public_key     = "<pubkey from above>"

[consensus]
validators = ["<pubkey from above>"]
```

### 3. Run

```bash
./build/chronos_node --config config/default.toml
```

You should see blocks finalizing:
```
Block finalized at height 1 round 0 hash abc123...
Block finalized at height 2 round 0 hash def456...
```

## Docker Deployment

For running on a server, see **[README-DOCKER.md](README-DOCKER.md)** for the full guide.

```bash
docker compose build
docker compose run --rm chronos wallet_cli generate-keys validator-1
docker compose run --rm chronos wallet_cli show-public validator-1
# Update config/node.toml with the public key
docker compose up -d
```

## Configuration

Key settings in `config/default.toml`:

| Setting | Default | Description |
|---------|---------|-------------|
| `[node] node_type` | `full` | `full` (LevelDB) or `light` (memory) |
| `[node] is_beacon_node` | `false` | `true` runs Layer 1 ChronosBeat-only beacon mode |
| `[consensus] slot_ms` | `500` | Block interval in ms (~2 blocks/sec) |
| `[consensus] bft_round_timeout_ms` | `5000` | Round failure timeout |
| `[consensus] validators` | `[]` | Validator public keys (hex Dilithium) |
| `[crypto] sign_alg` | `dilithium` | Signing algorithm |
| `[external_time_sources] time_backend` | `ntp` | `ntp` or `chrony` |

**Block rate tuning:**
- Single node: `slot_ms = 100` → ~10 blocks/sec
- Multi-node: `slot_ms` must be > 2× network RTT between validators
- `bft_round_timeout_ms` is independent — only activates when consensus stalls

## Key Management

Private keys are stored in `~/.chronos/keys/` (owner-only permissions), never in config files.

```bash
wallet_cli generate-keys <key-id>    # Generate and store a new key
wallet_cli list-keys                 # List stored keys
wallet_cli show-public <key-id>      # Show public key for config
```

## Documentation

| File | Contents |
|------|----------|
| [README-DOCKER.md](README-DOCKER.md) | Docker deployment guide |
| [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) | Architecture deep-dive, coding conventions |
| [USER_MANUAL.md](USER_MANUAL.md) | End-user guide (wallet, staking) |
| [SECURE_TIME_DESIGN.md](SECURE_TIME_DESIGN.md) | Proof-of-Time design |
| [SECURITY_STRIDE_ANALYSIS.md](SECURITY_STRIDE_ANALYSIS.md) | STRIDE security analysis |
| [UPGRADABILITY_STRATEGY.md](UPGRADABILITY_STRATEGY.md) | Protocol upgrade strategy |
| [CHANGELOG.md](CHANGELOG.md) | Version history |
| [TODO.md](TODO.md) | Remaining work |

## License

See [LICENSE](LICENSE) if present.
