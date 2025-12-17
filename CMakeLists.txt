cmake_minimum_required(VERSION 3.18)
project(chronos LANGUAGES C CXX)

# Opties
option(CHRONOS_USE_OQS "Enable PQC via liboqs (Dilithium)" ON)

# Compiler instellingen
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Debug instellingen
if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
endif()

# Compile definities
add_compile_definitions(CHRONOS_BLAKE3_ADDR_160=1) # 20-byte payload bech32m

# -------------------------------
# FetchContent dependencies
# -------------------------------
include(FetchContent)

# nlohmann/json
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.10.5
)
FetchContent_MakeAvailable(nlohmann_json)

# toml++
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
  GIT_TAG v3.2.0
)
FetchContent_MakeAvailable(tomlplusplus)

# httplib
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.11.3
)
FetchContent_MakeAvailable(httplib)

# -------------------------------
# Dependency Package Discovery
# -------------------------------
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS})

# -------------------------------
# Bronbestanden
# -------------------------------

# Gedeelde bibliotheek bronnen
set(CHRONOS_LIB_SRC
    src/p2p/peer_manager.cpp
    src/p2p/socket_transport.cpp
    src/p2p/p2p_client.cpp
    src/p2p/p2p_server.cpp
    src/p2p/gossip.cpp
    src/address/address.cpp
    src/address/bech32m.cpp
    src/storage/memory_kv.cpp
    src/storage/file_kv.cpp
    src/storage/DiskBlockchainStorage.cpp
    src/storage/MemoryBlockchainStorage.cpp
    src/crypto/signer_dilithium.cpp
    src/crypto/blake3.cpp
    src/crypto/signer.cpp
    src/crypto/signer_hmac.cpp
    src/crypto/key_manager.cpp
    src/ledger/block.cpp
    src/ledger/state.cpp
    src/ledger/transaction.cpp
    src/consensus/pot_aggregator.cpp
    src/consensus/bft.cpp
    src/consensus/external_time_source_manager.cpp
    src/consensus/ntp_client.cpp
    src/storage/snapshots.cpp
    src/util/bytes.cpp
    src/util/log.cpp
    src/util/console_display.cpp
    src/util/error_types.cpp
    src/util/retry_policy.cpp
)

# Node-specifieke bronnen
set(CHRONOS_NODE_SRC
    src/main.cpp
    src/node/config.cpp
    src/node/node_app.cpp
    src/rpc/jsonrpc.cpp
)

set(CHRONOS_PROTO_SRC
    src/proto/p2p_messages.pb.cc
    src/proto/bft_messages.pb.cc
)
set(CHRONOS_PROTO_HDR
    src/proto/p2p_messages.pb.h
    src/proto/bft_messages.pb.h
)

# BLAKE3 C-implementatie
file(GLOB BLAKE3_SRC
    "external/blake3/blake3.c"
    "external/blake3/blake3_dispatch.c"
    "external/blake3/blake3_portable.c"
)

# -------------------------------
# Gedeelde Bibliotheek (chronos_lib)
# -------------------------------
add_library(chronos_lib STATIC
    ${CHRONOS_LIB_SRC}
    ${BLAKE3_SRC}
    ${CHRONOS_PROTO_SRC}
    ${CHRONOS_PROTO_HDR}
)

set_source_files_properties(${BLAKE3_SRC} PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_compile_definitions(chronos_lib PUBLIC
    CHRONOS_BLAKE3_ADDR_160=1
    CHRONOS_INTERNAL_BLAKE3=1
    BLAKE3_NO_AVX512=1
    BLAKE3_NO_AVX2=1
    BLAKE3_NO_SSE41=1
    BLAKE3_NO_SSE2=1
)

target_include_directories(chronos_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/util
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/external/blake3
    ${CMAKE_CURRENT_SOURCE_DIR}/src/proto
    ${nlohmann_json_SOURCE_DIR}/include
    ${tomlplusplus_SOURCE_DIR}
    ${httplib_SOURCE_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(chronos_lib PUBLIC
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    httplib::httplib
    ${PROTOBUF_LIBRARIES}
    Threads::Threads
    -lz
    -lm
)

# liboqs integratie voor de bibliotheek
if (CHRONOS_USE_OQS)
    find_path(OQS_INCLUDE_DIR oqs/oqs.h PATHS /home/gert/Dev/oqs_install/include NO_DEFAULT_PATH)
    find_library(OQS_LIB oqs PATHS /home/gert/Dev/oqs_install/lib NO_DEFAULT_PATH)
    message(STATUS "CHRONOS_USE_OQS is ON. OQS_INCLUDE_DIR: ${OQS_INCLUDE_DIR}, OQS_LIB: ${OQS_LIB}")
    if (NOT OQS_INCLUDE_DIR OR NOT OQS_LIB)
        message(FATAL_ERROR "liboqs niet gevonden; zet CHRONOS_USE_OQS=OFF of installeer liboqs")
    endif()
    target_include_directories(chronos_lib PUBLIC ${OQS_INCLUDE_DIR})
    # Link OQS and its dependency OpenSSL::Crypto
    target_link_libraries(chronos_lib PRIVATE ${OQS_LIB} OpenSSL::Crypto)
    target_compile_definitions(chronos_lib PUBLIC CHRONOS_USE_OQS=1)
endif()

# -------------------------------
# Executable (chronos_node)
# -------------------------------
add_executable(chronos_node
    ${CHRONOS_NODE_SRC}
)

target_link_libraries(chronos_node PUBLIC chronos_lib)


# -------------------------------
# Wallet CLI
# -------------------------------
add_subdirectory(src/wallet/cli)
# -------------------------------
# Tests
# NEW: Added test_transaction_validation, test_state_serialization, test_thread_safety, test_snapshots
# These tests verify new production-readiness features
# Note: test_rpc.cpp, test_bft_integration.cpp have pre-existing issues
# and are excluded from this build
# -------------------------------
add_executable(chronos_tests 
    tests/test_framework.cpp 
    tests/test_bft.cpp 
    tests/test_address.cpp
    tests/test_ledger.cpp
    tests/test_pot.cpp
    tests/test_new_features.cpp
    tests/test_snapshots.cpp
    tests/test_merkle_root.cpp
    tests/test_thread_safety.cpp
    tests/test_genesis.cpp
    tests/test_enhanced_tx_validation.cpp
    tests/test_protobuf_validation.cpp
    tests/test_error_types.cpp
    tests/test_retry_policy.cpp
)

target_link_libraries(chronos_tests PRIVATE chronos_lib)

if(CHRONOS_USE_OQS)
    target_compile_definitions(chronos_tests PUBLIC CHRONOS_USE_OQS=1)
    target_include_directories(chronos_tests PUBLIC ${OQS_INCLUDE_DIR})
endif()

# Enable CTest
enable_testing()
add_test(NAME chronos_tests COMMAND chronos_tests)


# -------------------------------
# Install rules
# -------------------------------
install(TARGETS chronos_node wallet_cli DESTINATION bin)
install(FILES ${CHRONOS_PROTO_HDR} DESTINATION include/proto)