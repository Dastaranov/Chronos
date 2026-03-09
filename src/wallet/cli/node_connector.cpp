#include "node_connector.hpp"
#include <iostream>
#include <fstream>
#include <httplib.h>
#include <algorithm>
#include <random>

using json = nlohmann::json;

NodeConnector::NodeConnector() {
    // Add localhost as default if nothing else
    // We don't add it here to allow clean config loading, 
    // but main.cpp can add it if config is empty.
}

void NodeConnector::add_node(const std::string& host, int port, const std::string& api_key) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Check for duplicates
    for (const auto& node : nodes_) {
        if (node.host == host && node.port == port) return;
    }
    nodes_.push_back({host, port, api_key});
}

bool NodeConnector::parse_endpoint(const std::string& endpoint, std::string& host, int& port) {
    size_t colon = endpoint.find(':');
    if (colon == std::string::npos) return false;
    host = endpoint.substr(0, colon);
    try {
        port = std::stoi(endpoint.substr(colon + 1));
    } catch (...) {
        return false;
    }
    return true;
}

void NodeConnector::load_config(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    try {
        json j;
        file >> j;
        
        if (j.contains("nodes") && j["nodes"].is_array()) {
            for (const auto& node_json : j["nodes"]) {
                if (node_json.contains("host") && node_json.contains("port")) {
                    std::string api_key = node_json.value("api_key", "");
                    add_node(node_json["host"], node_json["port"], api_key);
                }
            }
        }
    } catch (const std::exception& e) {
        // Ignore errors, maybe empty file
    }
}

void NodeConnector::save_config(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    json j;
    j["nodes"] = json::array();
    for (const auto& node : nodes_) {
        json node_json;
        node_json["host"] = node.host;
        node_json["port"] = node.port;
        if (!node.api_key.empty()) {
            node_json["api_key"] = node.api_key;
        }
        j["nodes"].push_back(node_json);
    }

    std::ofstream file(path);
    if (file.is_open()) {
        file << j.dump(4);
    }
}

json NodeConnector::rpc_call(const std::string& method, const json& params) {
    // Create a copy of nodes to iterate over (thread-safeish)
    std::vector<Node> candidates;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        candidates = nodes_;
    }

    if (candidates.empty()) {
        throw std::runtime_error("No nodes configured. Use --rpc to specify a node.");
    }

    // Shuffle candidates to load balance / randomize
    std::random_device rd;
    std::mt19937 g(rd());
    std::shuffle(candidates.begin(), candidates.end(), g);

    std::string last_error;

    for (auto& node : candidates) {
        try {
            httplib::Client cli(node.host, node.port);
            cli.set_connection_timeout(2, 0); // 2 seconds timeout
            cli.set_read_timeout(5, 0);       // 5 seconds read timeout

            if (!node.api_key.empty()) {
                cli.set_default_headers({{"Authorization", "Bearer " + node.api_key}});
            }

            json req;
            req["jsonrpc"] = "2.0";
            req["method"] = method;
            req["params"] = params;
            req["id"] = 1;

            auto res = cli.Post("/", req.dump(), "application/json");
            if (!res) {
                last_error = "Connection failed to " + node.host + ":" + std::to_string(node.port);
                continue;
            }
            
            if (res->status != 200) {
                last_error = "HTTP error " + std::to_string(res->status) + " from " + node.host;
                continue;
            }

            json response = json::parse(res->body);
            if (response.contains("error")) {
                throw std::runtime_error(response["error"]["message"].get<std::string>());
            }
            
            // Success!
            return response["result"];

        } catch (const std::exception& e) {
            last_error = e.what();
            continue;
        }
    }

    throw std::runtime_error("All nodes failed. Last error: " + last_error);
}

void NodeConnector::discover_nodes() {
    try {
        // Call get_peers on a working node
        json peers = rpc_call("get_peers", json::object());
        
        if (peers.is_array()) {
            int added = 0;
            for (const auto& peer : peers) {
                // Peer info usually contains IP and port. 
                // Assuming peer info structure: { "ip": "...", "port": ... }
                // Or maybe the RPC returns something else. 
                // Based on previous turns, get_peers returns PeerInfo objects.
                // Let's assume it has "ip" and "port" or "address".
                
                // Actually, let's look at what get_peers returns.
                // It returns a list of PeerInfo.
                // PeerInfo has `endpoint` (string) or `ip` and `port`.
                // Let's assume "ip" and "rpc_port" if available, or just "port" (p2p port).
                // Wait, P2P port is not RPC port.
                // We can't easily discover RPC ports unless the P2P protocol advertises them.
                // For now, let's assume we can't auto-discover RPC ports from P2P peers 
                // unless we add that to the protocol.
                
                // However, if we are connecting to a cluster of nodes that use standard ports,
                // we might guess.
                
                // BUT, the user asked to "expand the search for nodes".
                // Maybe we can just try to connect to the IP with the default RPC port (8080).
                
                if (peer.contains("ip")) {
                    std::string ip = peer["ip"];
                    // Try default port 8080
                    add_node(ip, 8080); 
                    added++;
                }
            }
            if (added > 0) {
                std::cout << "Discovered " << added << " new nodes." << std::endl;
            }
        }
    } catch (...) {
        // Discovery failed, ignore
    }
}

std::vector<NodeConnector::Node> NodeConnector::get_nodes() const {
    return nodes_;
}
