#pragma once
#include "peer_store.hpp"
#include "gossip.hpp"
#include "proto/p2p_messages.pb.h"
#include <memory>
#include <thread>
#include <atomic>

namespace chrono_p2p {

class DiscoveryManager {
public:
    DiscoveryManager(
        std::shared_ptr<PeerStore> peer_store,
        std::shared_ptr<Gossip> gossip,
        const std::vector<std::string>& bootstrap_nodes,
        int max_peers = 50,
        int min_peers = 10,
        int discovery_interval_ms = 30000
    );
    
    ~DiscoveryManager();

    void start();
    void stop();
    
    // Periodic discovery tick
    void discover_peers();
    
    // Handle incoming peer exchange
    void handle_get_peers_request(const std::string& from_peer_id, uint32_t max_peers);
    void handle_get_peers_response(const std::vector<PeerAddress>& peers);
    
private:
    std::shared_ptr<PeerStore> peer_store_;
    std::shared_ptr<Gossip> gossip_;
    std::vector<std::string> bootstrap_nodes_;
    int max_peers_;
    int min_peers_;
    int discovery_interval_ms_;
    
    std::atomic<bool> running_{false};
    std::thread discovery_thread_;
    
    void discovery_loop();
    void query_bootstrap_nodes();
    void query_random_peers();
};

} // namespace chrono_p2p
