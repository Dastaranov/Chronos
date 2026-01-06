#include "test_framework.hpp"
#include "node/node_app.hpp"
#include "node/config.hpp"
#include "storage/IBlockchainStorage.hpp"
#include "storage/DiskBlockchainStorage.hpp"
#include "storage/MemoryBlockchainStorage.hpp"
#include "storage/file_kv.hpp"
#include "storage/memory_kv.hpp"
#include "ledger/block.hpp"
#include "util/bytes.hpp"
#include <fstream> // For checking file existence
#include <filesystem> // For creating directories

// Helper function to create a dummy block
chrono_ledger::Block create_dummy_block(const chrono_util::Bytes& prev_hash, uint64_t height) {
    std::vector<chrono_ledger::Transaction> txs;
    // Add some dummy transactions if needed for more realistic blocks
    return chrono_ledger::Block(prev_hash, height, 0, 0, txs); // NEW: Add consensus_time = 0, round = 0
}

// Helper to check if a file exists
bool file_exists(const std::string& name) {
    std::ifstream f(name.c_str());
    return f.good();
}

TEST_CASE(DiskBlockchainStorageStoresAndRetrievesBlocksAndMetadata, "DiskBlockchainStorage stores and retrieves blocks and metadata") {
    std::string test_file = "test_disk_blockchain.kv";
    // Clean up any previous test file
    std::remove(test_file.c_str());

    std::unique_ptr<chrono_storage::IKv> file_kv = std::make_unique<chrono_storage::FileKv>(test_file);
    chrono_storage::DiskBlockchainStorage storage(std::move(file_kv));

    // Test save and get block
    chrono_util::Bytes genesis_prev_hash(32, 0);
    chrono_ledger::Block genesis_block = create_dummy_block(genesis_prev_hash, 0);
    chrono_util::Bytes genesis_hash = genesis_block.get_header_hash();

    storage.saveBlock(genesis_block);
    std::optional<chrono_ledger::Block> retrieved_genesis = storage.getBlock(genesis_hash);
    ASSERT_TRUE(retrieved_genesis.has_value(), "Should retrieve genesis block");
    ASSERT_EQ(chrono_util::bytes_to_hex(retrieved_genesis->get_header_hash()), chrono_util::bytes_to_hex(genesis_hash), "Retrieved block hash should match");

    // Test save and get metadata
    chrono_util::Bytes test_key = chrono_util::string_to_bytes("TEST_METADATA_KEY");
    chrono_util::Bytes test_value = chrono_util::string_to_bytes("TEST_METADATA_VALUE");
    storage.saveMetadata(test_key, test_value);
    std::optional<chrono_util::Bytes> retrieved_value = storage.getMetadata(test_key);
    ASSERT_TRUE(retrieved_value.has_value(), "Should retrieve metadata value");
    ASSERT_EQ(chrono_util::bytes_to_hex(*retrieved_value), chrono_util::bytes_to_hex(test_value), "Retrieved metadata should match");

    // Clean up
    std::remove(test_file.c_str());
}

TEST_CASE(MemoryBlockchainStorageStoresAndRetrievesBlocksAndMetadata, "MemoryBlockchainStorage stores and retrieves blocks and metadata") {
    std::unique_ptr<chrono_storage::IKv> memory_kv = std::make_unique<chrono_storage::MemoryKv>();
    chrono_storage::MemoryBlockchainStorage storage(std::move(memory_kv));

    // Test save and get block
    chrono_util::Bytes genesis_prev_hash(32, 0);
    chrono_ledger::Block genesis_block = create_dummy_block(genesis_prev_hash, 0);
    chrono_util::Bytes genesis_hash = genesis_block.get_header_hash();

    storage.saveBlock(genesis_block);
    std::optional<chrono_ledger::Block> retrieved_genesis = storage.getBlock(genesis_hash);
    ASSERT_TRUE(retrieved_genesis.has_value(), "Should retrieve genesis block from memory");
    ASSERT_EQ(chrono_util::bytes_to_hex(retrieved_genesis->get_header_hash()), chrono_util::bytes_to_hex(genesis_hash), "Retrieved block hash should match from memory");

    // Test save and get metadata
    chrono_util::Bytes test_key = chrono_util::string_to_bytes("TEST_METADATA_KEY_MEM");
    chrono_util::Bytes test_value = chrono_util::string_to_bytes("TEST_METADATA_VALUE_MEM");
    storage.saveMetadata(test_key, test_value);
    std::optional<chrono_util::Bytes> retrieved_value = storage.getMetadata(test_key);
    ASSERT_TRUE(retrieved_value.has_value(), "Should retrieve metadata value from memory");
    ASSERT_EQ(chrono_util::bytes_to_hex(*retrieved_value), chrono_util::bytes_to_hex(test_value), "Retrieved metadata should match from memory");
}

TEST_CASE(NodeAppInitializesDiskBlockchainStorageForFULLNode, "NodeApp initializes DiskBlockchainStorage for FULL node") {
    chrono_node::Config cfg;
    cfg.node_type = chrono_node::NodeType::FULL;
    cfg.data_dir = "test_data_full";
    // Ensure test_data_full directory exists or is created by NodeApp
    // For this test, we just ensure the path is distinct
    std::filesystem::remove_all(cfg.data_dir); // Clean up any previous run's directory
    std::filesystem::create_directories(cfg.data_dir); // Create the directory

    // NodeApp constructor now initializes blockchain_storage_ internally
    chrono_node::NodeApp app(cfg);
    
    // Attempt to dynamically cast to DiskBlockchainStorage to verify type
    chrono_storage::DiskBlockchainStorage* disk_storage = 
        dynamic_cast<chrono_storage::DiskBlockchainStorage*>(app.getBlockchainStorage());
    ASSERT_TRUE(disk_storage != nullptr, "NodeApp should initialize DiskBlockchainStorage for FULL node");

    // Check if the underlying file is created
    ASSERT_TRUE(file_exists(cfg.data_dir + "/chronos_blockchain.kv"), "DiskBlockchainStorage file should exist");
    std::remove((cfg.data_dir + "/chronos_blockchain.kv").c_str());
    std::filesystem::remove_all(cfg.data_dir); // Clean up the directory
}

TEST_CASE(NodeAppInitializesMemoryBlockchainStorageForLIGHTNode, "NodeApp initializes MemoryBlockchainStorage for LIGHT node") {
    chrono_node::Config cfg;
    cfg.node_type = chrono_node::NodeType::LIGHT;
    cfg.data_dir = "test_data_light"; // Data dir is still passed but MemoryKv doesn't use it
    
    // NodeApp constructor now initializes blockchain_storage_ internally
    chrono_node::NodeApp app(cfg);

    // Attempt to dynamically cast to MemoryBlockchainStorage to verify type
    chrono_storage::MemoryBlockchainStorage* memory_storage = 
        dynamic_cast<chrono_storage::MemoryBlockchainStorage*>(app.getBlockchainStorage());
    ASSERT_TRUE(memory_storage != nullptr, "NodeApp should initialize MemoryBlockchainStorage for LIGHT node");
}

TEST_CASE(NodeAppAddBlockBehaviorForFULLNode, "NodeApp add_block behavior for FULL node") {
    std::string test_data_dir = "test_full_node_add_block";
    chrono_node::Config cfg;
    cfg.node_type = chrono_node::NodeType::FULL;
    cfg.data_dir = test_data_dir;
    std::filesystem::remove_all(cfg.data_dir); // Clean up any previous run's directory
    std::filesystem::create_directories(cfg.data_dir); // Create the directory

    chrono_node::NodeApp app(cfg);

    chrono_util::Bytes prev_hash(32, 0);
    chrono_ledger::Block block1 = create_dummy_block(prev_hash, 0); // Genesis
    app.add_block(block1);

    // Verify block is saved in DiskBlockchainStorage
    std::optional<chrono_ledger::Block> retrieved_block = app.getBlockchainStorage()->getBlock(block1.get_header_hash());
    ASSERT_TRUE(retrieved_block.has_value(), "Block should be saved for FULL node");
    ASSERT_EQ(chrono_util::bytes_to_hex(retrieved_block->get_header_hash()), chrono_util::bytes_to_hex(block1.get_header_hash()), "Saved block hash should match");

    // Verify metadata is updated
    std::optional<chrono_util::Bytes> last_hash_meta = app.getBlockchainStorage()->getMetadata(chrono_node::LAST_BLOCK_HASH_KEY);
    ASSERT_TRUE(last_hash_meta.has_value(), "LAST_BLOCK_HASH_KEY metadata should be updated");
    ASSERT_EQ(chrono_util::bytes_to_hex(*last_hash_meta), chrono_util::bytes_to_hex(block1.get_header_hash()), "LAST_BLOCK_HASH_KEY should match block1 hash");

    std::filesystem::remove_all(cfg.data_dir); // Clean up the directory
}

TEST_CASE(NodeAppAddBlockBehaviorForLIGHTNode, "NodeApp add_block behavior for LIGHT node") {
    std::string test_data_dir = "test_light_node_add_block";
    chrono_node::Config cfg;
    cfg.node_type = chrono_node::NodeType::LIGHT;
    cfg.data_dir = test_data_dir; // MemoryKv doesn't use this, but passed for consistency

    chrono_node::NodeApp app(cfg);

    chrono_util::Bytes prev_hash(32, 0);
    chrono_ledger::Block block1 = create_dummy_block(prev_hash, 0); // Genesis
    app.add_block(block1);

    // Verify block is NOT saved in MemoryBlockchainStorage (it should return nullopt)
    std::optional<chrono_ledger::Block> retrieved_block = app.getBlockchainStorage()->getBlock(block1.get_header_hash());
    ASSERT_TRUE(!retrieved_block.has_value(), "Block should NOT be saved for LIGHT node");

    // Verify metadata is updated (it should be in MemoryKv)
    std::optional<chrono_util::Bytes> last_hash_meta = app.getBlockchainStorage()->getMetadata(chrono_node::LAST_BLOCK_HASH_KEY);
    ASSERT_TRUE(last_hash_meta.has_value(), "LAST_BLOCK_HASH_KEY metadata should be updated for LIGHT node");
    ASSERT_EQ(chrono_util::bytes_to_hex(*last_hash_meta), chrono_util::bytes_to_hex(block1.get_header_hash()), "LAST_BLOCK_HASH_KEY should match block1 hash for LIGHT node");
}

// TODO: Add more tests for P2P message handling, handshake, and synchronization logic.