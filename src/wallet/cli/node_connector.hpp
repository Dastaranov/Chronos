#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <mutex>

class NodeConnector {
public:
    struct Node {
        std::string host;
        int port;
        std::string api_key;
        bool online = true;
        int failures = 0;
    };

    NodeConnector();

    // Add a node to the pool
    void add_node(const std::string& host, int port, const std::string& api_key = "");

    // Load nodes from a config file (simple text file or JSON)
    void load_config(const std::string& path);

    // Save known nodes to config
    void save_config(const std::string& path);

    // Perform an RPC call with failover
    nlohmann::json rpc_call(const std::string& method, const nlohmann::json& params);

    // Discover new nodes by asking current nodes
    void discover_nodes();

    // Get list of active nodes
    std::vector<Node> get_nodes() const;

private:
    std::vector<Node> nodes_;
    std::mutex mutex_;
    
    // Helper to parse "host:port" string
    bool parse_endpoint(const std::string& endpoint, std::string& host, int& port);
};
