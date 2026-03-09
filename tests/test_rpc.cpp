#include "test_framework.hpp"
#include "rpc/jsonrpc.hpp"
#include "node/node_app.hpp"
#include "ledger/state.hpp"
#include "storage/IBlockchainStorage.hpp"
#include "util/bytes.hpp"
#include "address/address.hpp"
#include "crypto/signer.hpp"
#include <memory>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>

// Mock NodeApp for testing JsonRpcServer
class MockNodeApp : public chrono_node::NodeApp {
public:
    // Expose NodeApp's constructor or provide a custom one for testing
    // For this mock, we only need to provide enough to satisfy JsonRpcServer's needs
    MockNodeApp(const chrono_node::Config& cfg) : chrono_node::NodeApp(cfg) {}

    // Mock members and methods that JsonRpcServer handlers will call
    chrono_util::Bytes last_block_hash_;
    uint64_t next_block_height_ = 1; // Start with height 1, so current is 0
    NodeStatus status_; // Public for easy access in tests
    
    // Mock for getBlockchainStorage
    class MockBlockchainStorage : public chrono_storage::IBlockchainStorage {
    public:
        void saveBlock(const chrono_ledger::Block& block) override {}
        std::optional<chrono_ledger::Block> getBlock(const chrono_util::Bytes& block_hash) const override { return std::nullopt; }
        std::optional<chrono_ledger::Block> getBlock(uint64_t height) const override { return std::nullopt; }
        void saveMetadata(const chrono_util::Bytes& key, const chrono_util::Bytes& value) override {}
        std::optional<chrono_util::Bytes> getMetadata(const chrono_util::Bytes& key) const override { return std::nullopt; }

        // Test specific: allow setting return values
        std::optional<chrono_ledger::Block> block_to_return;
        std::optional<chrono_ledger::Block> block_to_return_by_height;
    };
    MockBlockchainStorage mock_storage;

    chrono_storage::IBlockchainStorage* getBlockchainStorage() const override {
        return &mock_storage; // Return the mock storage
    }

    // Mock for get_mempool_const()
    std::vector<chrono_ledger::Transaction> mock_mempool;
    const std::vector<chrono_ledger::Transaction>& get_mempool_const() const override { return mock_mempool; }

    // Mock for add_transaction_to_mempool
    bool add_transaction_to_mempool(const chrono_ledger::Transaction& tx) override { 
        if (add_tx_to_mempool_success) {
            mock_mempool.push_back(tx); 
            return true;
        }
        return false;
    }
    bool add_tx_to_mempool_success = true;

    // Mock for publish_transaction
    void publish_transaction(const chrono_ledger::Transaction& tx) override {
        last_published_tx = tx;
    }
    std::optional<chrono_ledger::Transaction> last_published_tx;
};

// --- Test Cases ---

TEST_CASE(RPCGetStatus, "RPC get_status") {
    chrono_node::Config cfg;
    cfg.p2p_port = 8000;
    cfg.rpc_port = 9000;
    cfg.node_id = "test_node_id";

    // Setup MockNodeApp
    MockNodeApp mock_node_app(cfg);
    mock_node_app.status_.node_id = cfg.node_id;
    mock_node_app.status_.p2p_address = "127.0.0.1:8000";
    mock_node_app.status_.rpc_address = "127.0.0.1:9000";
    mock_node_app.status_.connected_peers = 5;
    mock_node_app.last_block_hash_ = chrono_util::hex_to_bytes("abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789");
    mock_node_app.next_block_height_ = 101; // Current height 100

    chrono_ledger::State state(std::make_unique<chrono_storage::MemoryKv>()); // Dummy state

    JsonRpcServer rpc_server(cfg.rpc_port, state, mock_node_app);

    json request = {{"jsonrpc", "2.0"}, {"method", "get_status"}, {"id", 1}};
    json response = rpc_server.handle_request(request.dump()); // Assume rpc_server has handle_request

    REQUIRE(response.contains("result"), "Response should contain 'result'");
    REQUIRE(response["result"].contains("node_id"), "Result should contain 'node_id'");
    REQUIRE(response["result"]["node_id"] == "test_node_id", "Node ID matches");
    REQUIRE(response["result"]["p2p_address"] == "127.0.0.1:8000", "P2P Address matches");
    REQUIRE(response["result"]["rpc_address"] == "127.0.0.1:9000", "RPC Address matches");
    REQUIRE(response["result"]["current_block_height"] == 100, "Current block height matches");
    REQUIRE(response["result"]["last_block_hash"] == "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789", "Last block hash matches");
    REQUIRE(response["result"]["connected_peers_count"] == 5, "Connected peers count matches");
    REQUIRE(response["result"]["is_syncing"] == false, "Is syncing status matches");
}

TEST_CASE(RPCGetBalance, "RPC get_balance") {
    chrono_node::Config cfg; // Dummy config
    MockNodeApp mock_node_app(cfg);
    chrono_ledger::State state(std::make_unique<chrono_storage::MemoryKv>());
    
    // Set a balance in the state mock
    state.set_balance("chronos_1abcdef", 1000);

    JsonRpcServer rpc_server(cfg.rpc_port, state, mock_node_app);

    json request = {{"jsonrpc", "2.0"}, {"method", "get_balance"}, {"params", {{"address", "chronos_1abcdef"}}}, {"id", 1}};
    json response = rpc_server.handle_request(request.dump());

    REQUIRE(response.contains("result"), "Response should contain 'result'");
    REQUIRE(response["result"].contains("address"), "Result should contain 'address'");
    REQUIRE(response["result"]["address"] == "chronos_1abcdef", "Address matches");
    REQUIRE(response["result"].contains("balance"), "Result should contain 'balance'");
    REQUIRE(response["result"]["balance"] == 1000, "Balance matches");

    // Test missing address parameter
    json bad_request = {{"jsonrpc", "2.0"}, {"method", "get_balance"}, {"id", 2}};
    json bad_response = rpc_server.handle_request(bad_request.dump());
    REQUIRE(bad_response.contains("error"), "Bad response should contain 'error'");
    REQUIRE(bad_response["error"]["code"] == -32602, "Error code for missing param matches");

    // Test invalid address format (assuming chrono_address::Address::from_string throws)
    json invalid_addr_request = {{"jsonrpc", "2.0"}, {"method", "get_balance"}, {"params", {{"address", "invalid_address"}}}, {"id", 3}};
    json invalid_addr_response = rpc_server.handle_request(invalid_addr_request.dump());
    REQUIRE(invalid_addr_response.contains("error"), "Invalid address response should contain 'error'");
    REQUIRE(invalid_addr_response["error"]["code"] == -32000, "Error code for invalid address matches");
}

TEST_CASE(RPCSendTransaction, "RPC send_transaction") {
    chrono_node::Config cfg; // Dummy config
    MockNodeApp mock_node_app(cfg);
    chrono_ledger::State state(std::make_unique<chrono_storage::MemoryKv>()); // Dummy state
    
    // RPC Server
    JsonRpcServer rpc_server(cfg.rpc_port, state, mock_node_app);

    // Mock a valid transaction for testing
    chrono_crypto::HmacSigner signer("test_key");
    chrono_address::Address from_addr = chrono_address::Address::from_string("chronos_1sender");
    chrono_address::Address to_addr = chrono_address::Address::from_string("chronos_1receiver");
    chrono_ledger::Transaction tx_to_send(from_addr, to_addr, 100, 0, 0); // Amount 100, fee 0, nonce 0
    tx_to_send.public_key = chrono_util::hex_to_bytes(signer.get_public_key().to_hex());
    tx_to_send.signature = signer.sign_message(tx_to_send.to_json_signable());

    json request = {
        {"jsonrpc", "2.0"}, 
        {"method", "send_transaction"}, 
        {"params", {
            {"from_address", "chronos_1sender"},
            {"to_address", "chronos_1receiver"},
            {"amount", 100},
            {"signature", tx_to_send.signature.to_hex()},
            {"public_key", chrono_util::bytes_to_hex(tx_to_send.public_key)}
        }}, 
        {"id", 1}
    };
    json response = rpc_server.handle_request(request.dump());

    REQUIRE(response.contains("result"), "Response should contain 'result'");
    REQUIRE(response["result"].contains("transaction_hash"), "Result should contain 'transaction_hash'");
    REQUIRE(response["result"]["transaction_hash"] == chrono_util::bytes_to_hex(tx_to_send.get_hash()), "Transaction hash matches");
    REQUIRE(response["result"]["status"] == "pending", "Transaction status is pending");

    // Verify NodeApp mocks were called
    REQUIRE(mock_node_app.mock_mempool.size() == 1, "Transaction should be added to NodeApp's mempool");
    ASSERT_EQ(mock_node_app.mock_mempool[0].get_hash(), tx_to_send.get_hash(), "Mempool transaction hash matches");
    REQUIRE(mock_node_app.last_published_tx.has_value(), "Transaction should be published");
    ASSERT_EQ(mock_node_app.last_published_tx->get_hash(), tx_to_send.get_hash(), "Published transaction hash matches");

    // Test invalid transaction (NodeApp rejects)
    mock_node_app.add_tx_to_mempool_success = false;
    json rejected_response = rpc_server.handle_request(request.dump());
    REQUIRE(rejected_response.contains("error"), "Rejected response should contain 'error'");
    REQUIRE(rejected_response["error"]["message"] == "Transaction rejected by node", "Rejection message matches");
    mock_node_app.add_tx_to_mempool_success = true; // Reset

    // Test missing parameters
    json missing_param_request = {{"jsonrpc", "2.0"}, {"method", "send_transaction"}, {"params", {{"from_address", "chronos_1sender"}}}, {"id", 2}};
    json missing_param_response = rpc_server.handle_request(missing_param_request.dump());
    REQUIRE(missing_param_response.contains("error"), "Missing param response should contain 'error'");
    REQUIRE(missing_param_response["error"]["code"] == -32602, "Error code for missing param matches");
}

TEST_CASE(RPCGetBlock, "RPC get_block") {
    chrono_node::Config cfg; // Dummy config
    MockNodeApp mock_node_app(cfg);
    chrono_ledger::State state(std::make_unique<chrono_storage::MemoryKv>()); // Dummy state
    
    JsonRpcServer rpc_server(cfg.rpc_port, state, mock_node_app);

    // Prepare a mock block
    chrono_util::Bytes prev_hash(32, 0x00);
    chrono_ledger::Block mock_block(prev_hash, 10, 1234567890, 0, {}); // Height 10, consensus_time, round
    mock_node_app.mock_storage.block_to_return = mock_block;
    mock_node_app.mock_storage.block_to_return_by_height = mock_block;

    // Test by height
    json request_height = {{"jsonrpc", "2.0"}, {"method", "get_block"}, {"params", {{"height", 10}}}, {"id", 1}};
    json response_height = rpc_server.handle_request(request_height.dump());
    REQUIRE(response_height.contains("result"), "Response by height should contain 'result'");
    REQUIRE(response_height["result"].contains("height"), "Result by height should contain 'height'");
    REQUIRE(response_height["result"]["height"] == 10, "Block height matches");

    // Test by hash
    mock_node_app.mock_storage.block_to_return = mock_block; // Reset for hash lookup
    json request_hash = {{"jsonrpc", "2.0"}, {"method", "get_block"}, {"params", {{"hash", chrono_util::bytes_to_hex(mock_block.get_header_hash())}}}, {"id", 2}};
    json response_hash = rpc_server.handle_request(request_hash.dump());
    REQUIRE(response_hash.contains("result"), "Response by hash should contain 'result'");
    REQUIRE(response_hash["result"].contains("hash"), "Result by hash should contain 'hash'"); // Assuming block.to_json adds "hash"
    // TODO: Need a better way to compare full block json.
    REQUIRE(response_hash["result"]["prev_block_hash"] == chrono_util::bytes_to_hex(mock_block.prev_block_hash), "Prev hash matches");


    // Test block not found
    mock_node_app.mock_storage.block_to_return = std::nullopt;
    mock_node_app.mock_storage.block_to_return_by_height = std::nullopt;
    json not_found_request = {{"jsonrpc", "2.0"}, {"method", "get_block"}, {"params", {{"height", 99}}}, {"id", 3}};
    json not_found_response = rpc_server.handle_request(not_found_request.dump());
    REQUIRE(not_found_response.contains("error"), "Not found response should contain 'error'");
    REQUIRE(not_found_response["error"]["code"] == -32001, "Error code for not found matches");
}

TEST_CASE(RPCGetTransaction, "RPC get_transaction") {
    chrono_node::Config cfg; // Dummy config
    MockNodeApp mock_node_app(cfg);
    chrono_ledger::State state(std::make_unique<chrono_storage::MemoryKv>()); // Dummy state
    
    JsonRpcServer rpc_server(cfg.rpc_port, state, mock_node_app);

    // Prepare a mock transaction
    chrono_address::Address from_addr = chrono_address::Address::from_string("chronos_tx_sender");
    chrono_address::Address to_addr = chrono_address::Address::from_string("chronos_tx_receiver");
    chrono_ledger::Transaction mock_tx(from_addr, to_addr, 50, 0, 0);
    mock_node_app.mock_mempool.push_back(mock_tx); // Add to mempool mock

    // Test transaction found in mempool
    json request_mempool = {{"jsonrpc", "2.0"}, {"method", "get_transaction"}, {"params", {{"hash", chrono_util::bytes_to_hex(mock_tx.get_hash())}}}, {"id", 1}};
    json response_mempool = rpc_server.handle_request(request_mempool.dump());
    REQUIRE(response_mempool.contains("result"), "Response from mempool should contain 'result'");
    REQUIRE(response_mempool["result"].contains("hash"), "Result should contain 'hash'");
    // TODO: Need a better way to compare full transaction json.
    REQUIRE(response_mempool["result"]["from_address"] == "chronos_tx_sender", "From address matches");

    // Test transaction not found
    json not_found_request = {{"jsonrpc", "2.0"}, {"method", "get_transaction"}, {"params", {{"hash", "not_found_hash_abcdef"}}}}, {"id", 2}};
    json not_found_response = rpc_server.handle_request(not_found_request.dump());
    REQUIRE(not_found_response.contains("error"), "Not found response should contain 'error'");
    REQUIRE(not_found_response["error"]["code"] == -32001, "Error code for not found matches");
}