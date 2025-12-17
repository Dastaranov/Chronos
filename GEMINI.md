# GEMINI.md

## Gemini CLI Configuratie
Deze instellingen zorgen voor consistente output en commentaarstijl bij gebruik van Gemini CLI.

gemini config set defaultModel gemini-pro
gemini config set temperature 0.06
gemini config set style.codeComments true
gemini config set style.commentLanguage nl
gemini config set style.commentDetail uitgebreid
gemini config set outputFormat markdown
gemini config set maxTokens 4096
gemini config set debug true

# Controleer of authenticatie correct is ingesteld
gemini auth status

------------------------------------------------------------

## Project Overview

Chronos is een blockchain node implementatie in C++20 met een modulaire architectuur. Belangrijke componenten zijn consensus, P2P-netwerk, ledger en cryptografie.

Key Features:
- C++20: Moderne C++ features.
- Protobuf: Voor binair P2P-transport.
- Modular Design: Logische structuur (src/consensus, src/p2p, etc.).
- Pluggable Cryptography: Ondersteunt standaard crypto en PQC via liboqs (Dilithium).
- Configuratie: TOML-bestanden voor instellingen (config/default.toml).
- Dependencies: Beheerd via CMake FetchContent (nlohmann/json, toml++, cpp-httplib).

------------------------------------------------------------

## Building and Running

### Prerequisites
- C++20-compatibele compiler (GCC 10+, Clang 12+).
- CMake (v3.18+).
- Protobuf (libprotoc ≥ 3.21.12).
- Optioneel: liboqs voor PQC.

Controleer versies:
cmake --version
protoc --version
gcc --version

------------------------------------------------------------

### Building

1. Clone de repository:
   git clone <repository-url>
   cd chronos

2. Configureer met CMake:
   mkdir build
   cd build
   cmake ..

   Voor PQC (Dilithium via liboqs):
   cmake .. -DCHRONOS_USE_OQS=ON \
            -DOQS_INCLUDE_DIR=/path/to/oqs/include \
            -DOQS_LIB=/path/to/oqs/lib

3. Build het project:
   make
   Dit genereert de executable: chronos_node.

------------------------------------------------------------

### Build Types
- Debug build:
  cmake .. -DCMAKE_BUILD_TYPE=Debug
- Release build:
  cmake .. -DCMAKE_BUILD_TYPE=Release

------------------------------------------------------------

### Running the Node
Met standaardconfiguratie:
./build/chronos_node

Met aangepaste configuratie:
./build/chronos_node --config /path/to/config.toml

------------------------------------------------------------

## Protobuf Integratie
Chronos gebruikt Protobuf voor P2P-berichten. De build is geautomatiseerd via CMake:

find_package(Protobuf REQUIRED)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/p2p_messages.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/bft_messages.proto
)
add_executable(chronos_node ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(chronos_node ${Protobuf_LIBRARIES})

Handmatig compileren (indien nodig):
protoc --cpp_out=. proto/p2p_messages.proto proto/bft_messages.proto

------------------------------------------------------------

## Development Conventions
- Coding Style: Houd bestaande stijl aan.
- Dependencies: Beheerd via CMake FetchContent. Voeg nieuwe dependencies toe in CMakeLists.txt en documenteer in Repository.md.
- Modularity: Nieuwe features moeten modulair zijn (bijv. nieuwe consensus in src/consensus).
- Configuratie: Gebruik TOML voor instellingen.

------------------------------------------------------------

## Code Style & Linting
Gebruik clang-format voor consistente code:
clang-format -i src/**/*.cpp src/**/*.h

------------------------------------------------------------

## Post-Quantum Cryptography
Chronos ondersteunt Dilithium via liboqs. Activeer met:
cmake .. -DCHRONOS_USE_OQS=ON

------------------------------------------------------------

## Troubleshooting
- Protobuf niet gevonden: Controleer protoc --version en CMake logs.
- liboqs niet gevonden: Zet CHRONOS_USE_OQS=OFF of installeer liboqs.
- Build errors: Controleer compiler versie en CMake minimum versie (≥ 3.18).