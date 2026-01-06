#include "discovery_manager.hpp"
#include "util/log.hpp"
#include "p2p_messages.pb.h"
#include <algorithm>
#include <random>

namespace chrono_p2p {

DiscoveryManager::DiscoveryManager(
    std::shared_ptr<PeerStore> peer_store,
    std::shared_ptr<Gossip> gossip,
    const std::vector<std::string>& bootstrap_nodes,
    int max_peers,
    int min_peers,
    int discovery_interval_ms
) : peer_store_(peer_store),
    gossip_(gossip),
    bootstrap_nodes_(bootstrap_nodes),
    max_peers_(max_peers),
    min_peers_(min_peers),
    discovery_interval_ms_(discovery_interval_ms) {}

DiscoveryManager::~DiscoveryManager() {
    stop();
}

void DiscoveryManager::start() {
    if (running_) return;
    running_ = true;
    discovery_thread_ = std::thread(&DiscoveryManager::discovery_loop, this);
    LOG_INFO(chrono_util::LogCategory::P2P, "DiscoveryManager started.");
}

void DiscoveryManager::stop() {
    if (!running_) return;
    running_ = false;
    if (discovery_thread_.joinable()) {
        discovery_thread_.join();
    }
    LOG_INFO(chrono_util::LogCategory::P2P, "DiscoveryManager stopped.");
}

void DiscoveryManager::discovery_loop() {
    while (running_) {
        discover_peers();
        std::this_thread::sleep_for(std::chrono::milliseconds(discovery_interval_ms_));
    }
}

void DiscoveryManager::discover_peers() {
    auto known_peers = peer_store_->get_all_peers();
    
    if (known_peers.size() < min_peers_) {
        LOG_INFO(chrono_util::LogCategory::P2P, "Known peers ({}) below minimum ({}), querying bootstrap nodes.", known_peers.size(), min_peers_);
        query_bootstrap_nodes();
    } else {
        query_random_peers();
    }
}

void DiscoveryManager::query_bootstrap_nodes() {
    for (const auto& node : bootstrap_nodes_) {
        size_t colon_pos = node.find(':');
        if (colon_pos != std::string::npos) {
            std::string host = node.substr(0, colon_pos);
            int port = std::stoi(node.substr(colon_pos + 1));
            
            // Attempt to connect (Gossip handles deduplication)
            gossip_->connect_to_peer(host, port);
            
            P2PMessage msg;
            auto* req = msg.mutable_get_peers_request();
            req->set_max_peers(max_peers_);
            
            // Send direct message to bootstrap node
            // Note: We use the full address string as ID if we don't have a node_id yet.
            // Ideally, handshake would give us the node_id, but for bootstrap we might just send to address.
            // SocketTransport::send_direct expects the key used in active_clients_ map, which is "ip:port".
            gossip_->send_direct(node, msg);
        }
    }
}

void DiscoveryManager::query_random_peers() {
    auto peers = peer_store_->get_recent_peers();
    if (peers.empty()) return;
    
    // Pick a random peer
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, peers.size() - 1);
    
    const auto& peer = peers[dis(gen)];
    
    P2PMessage msg;
    auto* req = msg.mutable_get_peers_request();
    req->set_max_peers(max_peers_);
    
    // We need to send to the connected peer ID.
    // PeerRecord stores node_id.
    gossip_->send_direct(peer.node_id, msg);
}

void DiscoveryManager::handle_get_peers_request(const std::string& from_peer_id, uint32_t max_peers) {
    LOG_INFO(chrono_util::LogCategory::P2P, "Received GetPeersRequest from {}", from_peer_id);
    
    auto peers = peer_store_->get_recent_peers();
    // Shuffle and limit
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(peers.begin(), peers.end(), gen);
    
    if (peers.size() > max_peers) {
        peers.resize(max_peers);
    }
    
    P2PMessage msg;
    auto* resp = msg.mutable_get_peers_response();
    for (const auto& p : peers) {
        auto* pa = resp->add_peers();
        pa->set_node_id(p.node_id);
        if (!p.addresses.empty()) {
            pa->set_address(p.addresses[0]);
        }
        pa->set_last_seen(std::chrono::duration_cast<std::chrono::seconds>(p.last_seen.time_since_epoch()).count());
        pa->set_is_validator(p.is_validator);
    }
    
    gossip_->send_direct(from_peer_id, msg);
}

void DiscoveryManager::handle_get_peers_response(const std::vector<PeerAddress>& peers) {
    LOG_INFO(chrono_util::LogCategory::P2P, "Received GetPeersResponse with {} peers", peers.size());
    
    for (const auto& p : peers) {
        PeerRecord record;
        record.node_id = p.node_id();
        if (record.node_id.empty()) record.node_id = p.address(); // Fallback
        
        record.addresses.push_back(p.address());
        record.last_seen = std::chrono::system_clock::time_point(std::chrono::seconds(p.last_seen()));
        record.is_validator = p.is_validator();
        
        peer_store_->add_peer(record);
    }
    peer_store_->save_to_disk();
}

} // namespace chrono_p2p
