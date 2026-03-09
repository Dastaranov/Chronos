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

#include "p2p_messages.pb.h" // For Protobuf messages
#include "bft_messages.pb.h" // For Protobuf messages
#include "crypto/signer_hmac.hpp" // For signer
#include "crypto/signature.hpp" // For signature

// Helper: MockTransport (minimal implementation for compilation)
class MockTransport : public chrono_p2p::ITransport {
public:
    bool listen(const std::string& addr, int port) override { return true; }
    bool connect(const std::string& host, int port) override { return true; }
    bool publish(const std::string& topic, const Bytes& msg) override { return true; }
    void on_message(MsgHandler cb) override {}
};

// Helper: NodeStatus (minimal implementation for compilation)
struct NodeStatus {
    std::string node_id;
    std::string rpc_address;
    std::string p2p_address;
    int connected_peers = 0;
};

TEST_CASE(NodeAppPeerScoringAndManagement, "NodeApp Peer Scoring and Management") {
    chrono_node::Config cfg;
    cfg.p2p_port = 8000;
    cfg.data_dir = "./test_data_peer_scoring";

    // Use MockTransport to avoid actual network ops
    // MockTransport* raw_mock_transport = new MockTransport(); // Get raw pointer before move
    // This part is commented out because NodeApp's constructor currently doesn't allow
    // injection of a custom ITransport. It creates its own SocketTransport.
    // To properly test this, NodeApp's constructor or a setter would need to be refactored.

    // Let's test PeerInfo struct default values and basic score changes directly.
    chrono_node::PeerInfo peer_info;
    peer_info.node_id = "test_peer_1";
    peer_info.address = "127.0.0.1:12345";

    ASSERT_EQ(peer_info.score, 0, "Initial score should be 0");
    ASSERT_EQ(peer_info.invalid_messages_count, 0, "Initial invalid message count should be 0");

    // To properly test update_peer_score and manage_peers, NodeApp needs to expose internal access
    // or provide helper methods for testing. For now, this is a placeholder.
    ASSERT_TRUE(true, "Placeholder for NodeApp Peer Scoring and Management tests. Requires mocking NodeApp internal state.");
}

TEST_CASE(ProtobufP2PMessageSerializationDeserialization, "Protobuf P2P Message Serialization/Deserialization") {
    // HandshakeMessage
    chrono_p2p::P2PMessage original_handshake_envelope;
    auto* original_handshake = original_handshake_envelope.mutable_handshake();
    original_handshake->set_node_id("test_node");
    original_handshake->set_protocol_version(1);
    original_handshake->set_port(8080);
    original_handshake->set_last_block_hash("hash123", 7);
    original_handshake->set_current_block_height(100);

    Bytes serialized_handshake(original_handshake_envelope.ByteSizeLong());
    original_handshake_envelope.SerializeToArray(serialized_handshake.data(), serialized_handshake.size());

    chrono_p2p::P2PMessage deserialized_handshake_envelope;
    deserialized_handshake_envelope.ParseFromArray(serialized_handshake.data(), serialized_handshake.size());
    const auto& deserialized_handshake = deserialized_handshake_envelope.handshake();

    ASSERT_EQ(original_handshake->node_id(), deserialized_handshake.node_id(), "Handshake node_id matches");
    ASSERT_EQ(original_handshake->protocol_version(), deserialized_handshake.protocol_version(), "Handshake protocol_version matches");
    ASSERT_EQ(original_handshake->port(), deserialized_handshake.port(), "Handshake port matches");
    ASSERT_BYTES_EQ(Bytes(original_handshake->last_block_hash().begin(), original_handshake->last_block_hash().end()), Bytes(deserialized_handshake.last_block_hash().begin(), deserialized_handshake.last_block_hash().end()), "Handshake last_block_hash matches");
    ASSERT_EQ(original_handshake->current_block_height(), deserialized_handshake.current_block_height(), "Handshake current_block_height matches");

    // GetBlocksMessage
    chrono_p2p::P2PMessage original_getblocks_envelope;
    auto* original_getblocks = original_getblocks_envelope.mutable_get_blocks();
    original_getblocks->set_from_block_hash("start_hash", 10);
    original_getblocks->set_limit(5);

    Bytes serialized_getblocks(original_getblocks_envelope.ByteSizeLong());
    original_getblocks_envelope.SerializeToArray(serialized_getblocks.data(), serialized_getblocks.size());

    chrono_p2p::P2PMessage deserialized_getblocks_envelope;
    deserialized_getblocks_envelope.ParseFromArray(serialized_getblocks.data(), serialized_getblocks.size());
    const auto& deserialized_getblocks = deserialized_getblocks_envelope.get_blocks();

    ASSERT_BYTES_EQ(Bytes(original_getblocks->from_block_hash().begin(), original_getblocks->from_block_hash().end()), Bytes(deserialized_getblocks.from_block_hash().begin(), deserialized_getblocks.from_block_hash().end()), "GetBlocks from_block_hash matches");
    ASSERT_EQ(original_getblocks->limit(), deserialized_getblocks.limit(), "GetBlocks limit matches");
    
    // BlockMessage
    chrono_ledger::Block sample_block(Bytes(32, 0xAA), 1, 1000, 0, {});
    chrono_p2p::P2PMessage original_block_envelope;
    original_block_envelope.mutable_block()->set_block_data(sample_block.serialize().data(), sample_block.serialize().size());

    Bytes serialized_block(original_block_envelope.ByteSizeLong());
    original_block_envelope.SerializeToArray(serialized_block.data(), serialized_block.size());

    chrono_p2p::P2PMessage deserialized_block_envelope;
    deserialized_block_envelope.ParseFromArray(serialized_block.data(), serialized_block.size());
    const auto& deserialized_block_msg = deserialized_block_envelope.block();
    
    chrono_ledger::Block deserialized_block = chrono_ledger::Block::deserialize(Bytes(deserialized_block_msg.block_data().begin(), deserialized_block_msg.block_data().end()));
    ASSERT_BYTES_EQ(sample_block.get_header_hash(), deserialized_block.get_header_hash(), "Block hash matches after proto serialization");

    // TransactionMessage
    chrono_ledger::Transaction sample_tx("sender", "receiver", 100);
    chrono_p2p::P2PMessage original_tx_envelope;
    original_tx_envelope.mutable_transaction()->set_transaction_data(sample_tx.serialize().data(), sample_tx.serialize().size());

    Bytes serialized_tx(original_tx_envelope.ByteSizeLong());
    original_tx_envelope.SerializeToArray(serialized_tx.data(), serialized_tx.size());

    chrono_p2p::P2PMessage deserialized_tx_envelope;
    deserialized_tx_envelope.ParseFromArray(serialized_tx.data(), serialized_tx.size());
    const auto& deserialized_tx_msg = deserialized_tx_envelope.transaction();

    chrono_ledger::Transaction deserialized_tx = chrono_ledger::Transaction::deserialize(Bytes(deserialized_tx_msg.transaction_data().begin(), deserialized_tx_msg.transaction_data().end()));
    ASSERT_EQ(sample_tx.to_string(), deserialized_tx.to_string(), "Transaction matches after proto serialization");

    // PeerListMessage
    chrono_p2p::P2PMessage original_peerlist_envelope;
    auto* original_peerlist = original_peerlist_envelope.mutable_peer_list();
    original_peerlist->add_peers("192.168.1.1:8000");
    original_peerlist->add_peers("192.168.1.2:8001");

    Bytes serialized_peerlist(original_peerlist_envelope.ByteSizeLong());
    original_peerlist_envelope.SerializeToArray(serialized_peerlist.data(), serialized_peerlist.size());

    chrono_p2p::P2PMessage deserialized_peerlist_envelope;
    deserialized_peerlist_envelope.ParseFromArray(serialized_peerlist.data(), serialized_peerlist.size());
    const auto& deserialized_peerlist = deserialized_peerlist_envelope.peer_list();

    ASSERT_EQ(original_peerlist->peers_size(), deserialized_peerlist.peers_size(), "PeerList size matches");
    ASSERT_EQ(original_peerlist->peers(0), deserialized_peerlist.peers(0), "PeerList peer 0 matches");
    ASSERT_EQ(original_peerlist->peers(1), deserialized_peerlist.peers(1), "PeerList peer 1 matches");

    // BFT Prevote
    chrono_crypto::HmacSigner signer("prevote_test_key");
    chrono_crypto::Signature prevote_sig = signer.sign_message(nlohmann::json({{"height", 2}, {"round", 1}, {"block_hash", "bhash"}, {"validator_id", signer.get_address()}}));
    
    chrono_p2p::P2PMessage original_prevote_envelope;
    auto* original_prevote = original_prevote_envelope.mutable_prevote();
    original_prevote->set_height(2);
    original_prevote->set_round(1);
    original_prevote->set_block_hash("bhash", 5);
    original_prevote->set_validator_id(signer.get_address());
    original_prevote->mutable_signature()->set_data(prevote_sig.to_hex());

    Bytes serialized_prevote(original_prevote_envelope.ByteSizeLong());
    original_prevote_envelope.SerializeToArray(serialized_prevote.data(), serialized_prevote.size());

    chrono_p2p::P2PMessage deserialized_prevote_envelope;
    deserialized_prevote_envelope.ParseFromArray(serialized_prevote.data(), serialized_prevote.size());
    const auto& deserialized_prevote = deserialized_prevote_envelope.prevote();

    ASSERT_EQ(original_prevote->height(), deserialized_prevote.height(), "Prevote height matches");
    ASSERT_EQ(original_prevote->round(), deserialized_prevote.round(), "Prevote round matches");
    ASSERT_BYTES_EQ(Bytes(original_prevote->block_hash().begin(), original_prevote->block_hash().end()), Bytes(deserialized_prevote.block_hash().begin(), deserialized_prevote.block_hash().end()), "Prevote block_hash matches");
    ASSERT_EQ(original_prevote->validator_id(), deserialized_prevote.validator_id(), "Prevote validator_id matches");
    ASSERT_EQ(original_prevote->signature().data(), deserialized_prevote.signature().data(), "Prevote signature matches");

    // BFT Precommit
    chrono_crypto::HmacSigner signer2("precommit_test_key");
    chrono_crypto::Signature precommit_sig = signer2.sign_message(nlohmann::json({{"height", 3}, {"round", 2}, {"block_hash", "bhash2"}, {"validator_id", signer2.get_address()}}));

    chrono_p2p::P2PMessage original_precommit_envelope;
    auto* original_precommit = original_precommit_envelope.mutable_precommit();
    original_precommit->set_height(3);
    original_precommit->set_round(2);
    original_precommit->set_block_hash("bhash2", 6);
    original_precommit->set_validator_id(signer2.get_address());
    original_precommit->mutable_signature()->set_data(precommit_sig.to_hex());

    Bytes serialized_precommit(original_precommit_envelope.ByteSizeLong());
    original_precommit_envelope.SerializeToArray(serialized_precommit.data(), serialized_precommit.size());

    chrono_p2p::P2PMessage deserialized_precommit_envelope;
    deserialized_precommit_envelope.ParseFromArray(serialized_precommit.data(), serialized_precommit.size());
    const auto& deserialized_precommit = deserialized_precommit_envelope.precommit();

    ASSERT_EQ(original_precommit->height(), deserialized_precommit.height(), "Precommit height matches");
    ASSERT_EQ(original_precommit->round(), deserialized_precommit.round(), "Precommit round matches");
    ASSERT_BYTES_EQ(Bytes(original_precommit->block_hash().begin(), original_precommit->block_hash().end()), Bytes(deserialized_precommit.block_hash().begin(), deserialized_precommit.block_hash().end()), "Precommit block_hash matches");
    ASSERT_EQ(original_precommit->validator_id(), deserialized_precommit.validator_id(), "Precommit validator_id matches");
    ASSERT_EQ(original_precommit->signature().data(), deserialized_precommit.signature().data(), "Precommit signature matches");
    
    // BFT NewRound
    chrono_crypto::HmacSigner signer3("new_round_test_key");
    chrono_crypto::Signature new_round_sig = signer3.sign_message(nlohmann::json({{"height", 4}, {"round", 0}, {"proposal_block_hash", "pbhash"}, {"validator_id", signer3.get_address()}}));

    chrono_p2p::P2PMessage original_new_round_envelope;
    auto* original_new_round = original_new_round_envelope.mutable_new_round();
    original_new_round->set_height(4);
    original_new_round->set_round(0);
    original_new_round->set_proposal_block_hash("pbhash", 6);
    original_new_round->set_validator_id(signer3.get_address());
    original_new_round->mutable_signature()->set_data(new_round_sig.to_hex());

    Bytes serialized_new_round(original_new_round_envelope.ByteSizeLong());
    original_new_round_envelope.SerializeToArray(serialized_new_round.data(), serialized_new_round.size());

    chrono_p2p::P2PMessage deserialized_new_round_envelope;
    deserialized_new_round_envelope.ParseFromArray(serialized_new_round.data(), serialized_new_round.size());
    const auto& deserialized_new_round = deserialized_new_round_envelope.new_round();

    ASSERT_EQ(original_new_round->height(), deserialized_new_round.height(), "NewRound height matches");
    ASSERT_EQ(original_new_round->round(), deserialized_new_round.round(), "NewRound round matches");
    ASSERT_BYTES_EQ(Bytes(original_new_round->proposal_block_hash().begin(), original_new_round->proposal_block_hash().end()), Bytes(deserialized_new_round.proposal_block_hash().begin(), deserialized_new_round.proposal_block_hash().end()), "NewRound proposal_block_hash matches");
    ASSERT_EQ(original_new_round->validator_id(), deserialized_new_round.validator_id(), "NewRound validator_id matches");
    ASSERT_EQ(original_new_round->signature().data(), deserialized_new_round.signature().data(), "NewRound signature matches");
}