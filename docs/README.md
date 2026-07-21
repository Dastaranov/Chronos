# Chronos Blockchain Node

[![Build and Test](https://github.com/Dastaranov/Chronos/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/Dastaranov/Chronos/actions/workflows/build-and-test.yml)
[![Memory Check](https://github.com/Dastaranov/Chronos/actions/workflows/memory-check.yml/badge.svg)](https://github.com/Dastaranov/Chronos/actions/workflows/memory-check.yml)
[![CodeQL Analysis](https://github.com/Dastaranov/Chronos/actions/workflows/codeql-analysis.yml/badge.svg)](https://github.com/Dastaranov/Chronos/actions/workflows/codeql-analysis.yml)

# Chronos

Chronos is een moderne, in C++20 geschreven blockchain-node. Het systeem is ontworpen met een sterke focus op post-kwantum beveiliging, robuuste Byzantine Fault Tolerance (BFT) consensus en uiterst efficiënte data-opslag.

## Kernfuncties

* **Post-Kwantum Cryptografie:** P2P-communicatie is versleuteld met Kyber/AES-256-GCM en transacties worden ondertekend via CRYSTALS-Dilithium.
* **BFT Consensus & Proof-of-Time:** Netwerkovereenstemming met ondersteuning voor geavanceerde tijdsynchronisatie (voorbereid op NTS en atoomklokken).
* **Modulaire Opslag:** Een schaalbare opslaglaag aangedreven door LevelDB, met efficiënte Protobuf-serialisatie.
* **Uitgebreide Tooling:** Inclusief CLI-tools (`node_cli`, `wallet_cli`) voor laagdrempelig netwerk- en sleutelbeheer.

## Documentatie

Alle uitgebreide technische documentatie, architectuurnotities en logica bevinden zich in de [`/docs`](docs/) map. 

* [Architectuur & C++ Modules](docs/architecture/)
* [Consensus & Proof-of-Time](docs/consensus/)
* [Cryptografie Implementaties](docs/cryptography/)
* [API & Command Line Tools](docs/api/)

## Quickstart & Build Instructies

*Vereisten: C++20 compatibele compiler, CMake, LevelDB, Protobuf.*

```bash
# Clone de repository
git clone [https://github.com/dastaranov/chronos.git](https://github.com/dastaranov/chronos.git)
cd chronos

# Bouw het project
mkdir build && cd build
cmake ..
make