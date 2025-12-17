**Instructies:**
Manueel uit te voeren.
1. Protobuf commands
2. Cmake

**Protobuf Commands**
protoc --proto_path=proto --cpp_out=. proto/bft_messages.proto
protoc --proto_path=proto --cpp_out=. proto/p2p_messages.proto

**CMake**
rm -rf build
mkdir build
cmake -S . -B build

**Wallet CPP uitvoeren**
./build/src/wallet/cli/wallet_cli generate-keys






${CMAKE_CURRENT_BINARY_DIR} # Voor gegenereerde Protobuf headers


**oude protobuf integratie cmake**
# -------------------------------
# Protobuf integratie
# -------------------------------
find_package(Protobuf REQUIRED)
if (Protobuf_VERSION VERSION_LESS "3.0.0")
    message(FATAL_ERROR "Protobuf 3.x of hoger vereist")
endif()

if(NOT PROTOBUF_PROTOC_EXECUTABLE)
    message(FATAL_ERROR "protoc niet gevonden. Installeer Protobuf of geef pad op.")
endif()

include_directories(${Protobuf_INCLUDE_DIRS})

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/p2p_messages.proto
    ${CMAKE_CURRENT_SOURCE_DIR}/proto/bft_messages.proto
    PROTOC_EXECUTABLE ${PROTOBUF_PROTOC_EXECUTABLE}
)