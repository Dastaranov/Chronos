//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file jsonrpc.cpp
 * @brief Implements the JsonRpcServer class for handling JSON-RPC 2.0 requests.
 *
 * This file contains the implementation of the `JsonRpcServer` class, which
 * provides an HTTP-based JSON-RPC 2.0 interface for external clients to interact
 * with the Chronos blockchain node. It handles parsing incoming JSON-RPC requests,
 * dispatching them to registered handler functions, and formatting responses.
 * It integrates with the node's ledger state and transaction mempool.
 */

#include "rpc/jsonrpc.hpp"
#include "util/log.hpp"
#include "address/address.hpp"
#include "crypto/signer_hmac.hpp"
#include "node/node_app.hpp" // Added for NodeApp reference
#include "storage/IBlockchainStorage.hpp" // Added for IBlockchainStorage

namespace chrono_node { // Forward declaration for NodeApp

/**
 * @brief Constructs a JsonRpcServer object.
 *
 * Initializes the RPC server with a reference to the current ledger state and the
 * main node application instance. Method handlers are registered in the `serve()` method.
 *
 * @param port The port number on which the RPC server will listen.
 * @param api_key Optional API key for authentication.
 * @param state A reference to the `chrono_ledger::State` object that this RPC server will interact with.
 * @param node_app A reference to the main `chrono_node::NodeApp` instance.
 */
JsonRpcServer::JsonRpcServer(int port, const std::string& api_key, chrono_ledger::State& state, chrono_node::NodeApp& node_app)
    : port_(port), api_key_(api_key), state_(state), node_app_(node_app) {
    // Constructor is now intentionally empty, registration moved to serve()
}

/**
 * @brief Registers a new RPC method with its handler function.
 *
 * This method allows adding new RPC endpoints to the server. When a client
 * calls the specified `method`, the provided `fn` (function) will be executed.
 *
 * @param method The name of the RPC method (e.g., "get_balance", "send_transaction").
 * @param fn The handler function that will be called when the method is invoked.
 *           It takes a JSON object of parameters and returns a JSON object as a result.
 */
void JsonRpcServer::add(const std::string& method, std::function<json(const json&)> fn) {
    handlers_[method] = std::move(fn);
}

/**
 * @brief Starts the HTTP server to listen for incoming RPC requests.
 *
 * This method binds the server to the specified `port` and begins listening
 * for HTTP requests. It processes incoming requests as JSON-RPC 2.0 calls
 * and dispatches them to the registered handlers. It also handles various
 * error conditions, such as parse errors, invalid requests, and method not found.
 */
void JsonRpcServer::serve() {
    // Register handlers at the beginning of serve, just before listening
    add("send_transaction", [this](const json& params) {
        return handle_send_transaction(params);
    });
    add("get_balance", [this](const json& params) {
        return handle_get_balance(params);
    });
    add("get_status", [this](const json& params) {
        return handle_get_status(params);
    });
    add("get_block", [this](const json& params) {
        return handle_get_block(params);
    });
    add("get_transaction", [this](const json& params) {
        return handle_get_transaction(params);
    });
    add("get_nonce", [this](const json& params) {
        return handle_get_nonce(params);
    });
    add("get_candidates", [this](const json& params) {
        return handle_get_candidates(params);
    });
    add("get_peers", [this](const json& params) {
        return handle_get_peers(params);
    });
    add("get_mempool", [this](const json& params) {
        return handle_get_mempool(params);
    });

    http_server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\":\"ok\"}", "application/json");
    });

    http_server_.Post("/", [this](const httplib::Request& req, httplib::Response& res) {
        // Check API Key if configured
        if (!api_key_.empty()) {
            bool authorized = false;
            if (req.has_header("Authorization")) {
                std::string auth = req.get_header_value("Authorization");
                // Support "Bearer <key>" or just "<key>"
                if (auth == "Bearer " + api_key_ || auth == api_key_) {
                    authorized = true;
                }
            }
            
            if (!authorized) {
                res.status = 401;
                res.set_content("Unauthorized: Invalid or missing API Key", "text/plain");
                return;
            }
        }

        json response_json;
        try {
            json request_json = json::parse(req.body);

            // Validate JSON-RPC 2.0 request structure
            if (request_json.count("jsonrpc") && request_json["jsonrpc"] == "2.0" && request_json.count("method")) {
                std::string method = request_json["method"].get<std::string>();
                json params = request_json.count("params") ? request_json["params"] : json::object();
                json id = request_json.count("id") ? request_json["id"] : json(nullptr);

                // Find and execute the registered handler
                auto it = handlers_.find(method);
                if (it != handlers_.end()) {
                    json result = it->second(params);
                    response_json["jsonrpc"] = "2.0";
                    response_json["result"] = result;
                    response_json["id"] = id;
                } else {
                    // Method not found error
                    response_json = {
                        {"jsonrpc", "2.0"},
                        {"error", {{"code", -32601}, {"message", "Method not found"}}},
                        {"id", id}
                    };
                }
            } else {
                // Invalid Request error
                response_json = {
                    {"jsonrpc", "2.0"},
                    {"error", {{"code", -32600}, {"message", "Invalid Request"}}},
                    {"id", nullptr}
                };
            }
        } catch (const json::parse_error& e) {
            // Parse error for malformed JSON
            response_json = {
                {"jsonrpc", "2.0"},
                {"error", {{"code", -32700}, {"message", "Parse error"}}},
                {"id", nullptr}
            };
            LOG_ERROR(chrono_util::LogCategory::GENERAL, "JSON-RPC Parse error: {}", e.what());
        } catch (const std::exception& e) {
            // General server error
            response_json = {
                {"jsonrpc", "2.0"},
                {"error", {{"code", -32000}, {"message", "Server error"}}},
                {"id", nullptr}
            };
            LOG_ERROR(chrono_util::LogCategory::GENERAL, "JSON-RPC Server error: {}", e.what());
        }

        res.set_content(response_json.dump(), "application/json");
    });

    LOG_INFO(chrono_util::LogCategory::GENERAL, "JSON-RPC server listening on {}:{}", node_app_.cfg_.rpc_bind_ip, port_);
    if (!http_server_.listen(node_app_.cfg_.rpc_bind_ip.c_str(), port_)) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to start HTTP server on {}:{}", node_app_.cfg_.rpc_bind_ip, port_);
    }
}

/**
 * @brief Handles the "send_transaction" RPC request.
 *
 * This method parses transaction parameters from the incoming JSON, constructs a `Transaction` object,
 * and then adds it to the node's mempool and publishes it to the P2P network.
 * It returns a success status or an error message if parsing or processing fails.
 *
 * @param params A JSON object containing `sender_pub_key`, `recipient_pub_key`, `amount`, `fee`, `nonce`, and `signature`.
 * @return A JSON object indicating the status of the transaction submission.
 */
json JsonRpcServer::handle_send_transaction(const json& params) {
    LOG_INFO(chrono_util::LogCategory::GENERAL, "Received send_transaction request: {}", params.dump());
    
    try {
        // Extract transaction details from JSON parameters
        std::string sender_pub_key_hex = params.at("sender_pub_key").get<std::string>();
        uint64_t amount = params.at("amount").get<uint64_t>();
        uint64_t fee = params.at("fee").get<uint64_t>();
        uint64_t nonce = params.at("nonce").get<uint64_t>();
        std::string signature_hex = params.at("signature").get<std::string>();

        // Convert hex strings to Bytes and then to Address/Signature
        Bytes sender_pub_key = chrono_util::hex_to_bytes(sender_pub_key_hex);
        chrono_address::Address sender_addr(sender_pub_key);
        
        chrono_address::Address recipient_addr;
        if (params.contains("recipient_address")) {
             recipient_addr = chrono_address::Address(params.at("recipient_address").get<std::string>());
        } else if (params.contains("recipient_pub_key")) {
             recipient_addr = chrono_address::Address(chrono_util::hex_to_bytes(params.at("recipient_pub_key").get<std::string>()));
        } else {
             return {{"status", "error"}, {"message", "Missing recipient_address or recipient_pub_key"}};
        }
        
        // Create transaction object without signature first
        chrono_ledger::Transaction tx(sender_addr, recipient_addr, amount, fee, nonce, sender_pub_key);
        // Then set the signature
        tx.signature = chrono_util::hex_to_bytes(signature_hex);

        // Add transaction to NodeApp's mempool and publish it
        node_app_.add_transaction_to_mempool(tx);
        node_app_.publish_transaction(tx);

        return {{"status", "success"}, {"message", "Transaction added to mempool and published"}};
    } catch (const json::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to parse send_transaction parameters: {}", e.what());
        return {{"status", "error"}, {"message", "Invalid transaction parameters", "details", e.what()}};
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Error processing send_transaction: {}", e.what());
        return {{"status", "error"}, {"message", "Internal server error", "details", e.what()}};
    }
}

/**
 * @brief Handles the "get_balance" RPC request.
 *
 * This method retrieves the current balance of a specified address from the ledger state.
 * It validates the address format before querying the state.
 *
 * @param params A JSON object containing the "address" parameter.
 * @return A JSON object containing the balance or an error message if the address is invalid or not found.
 */
json JsonRpcServer::handle_get_balance(const json& params) {
    try {
        if (!params.contains("address")) {
            return {{"status", "error"}, {"message", "Missing 'address' parameter"}};
        }
        std::string address_str = params["address"].get<std::string>();
        
        // Validate address format before querying state
        chrono_address::Address addr(address_str);
        if (!addr.is_valid()) {
            LOG_WARN(chrono_util::LogCategory::GENERAL, "Received get_balance request with invalid address format: {}", address_str);
            return {{"status", "error"}, {"message", "Invalid address format"}};
        }

        uint64_t balance = state_.get_balance(address_str);
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Received get_balance request for {}: {}", address_str, balance);
        return {{"status", "success"}, {"address", address_str}, {"balance", balance}};
    } catch (const json::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to parse get_balance parameters: {}", e.what());
        return {{"status", "error"}, {"message", "Invalid parameters", "details", e.what()}};
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Error processing get_balance: {}", e.what());
        return {{"status", "error"}, {"message", "Internal server error", "details", e.what()}};
    }
}

json JsonRpcServer::handle_get_nonce(const json& params) {
    try {
        if (!params.contains("address")) {
            return {{"status", "error"}, {"message", "Missing 'address' parameter"}};
        }
        std::string address_str = params["address"].get<std::string>();
        
        // Validate address format
        chrono_address::Address addr(address_str);
        if (!addr.is_valid()) {
            return {{"status", "error"}, {"message", "Invalid address format"}};
        }

        uint64_t nonce = state_.get_nonce(address_str);
        return {{"status", "success"}, {"address", address_str}, {"nonce", nonce}};
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Error processing get_nonce: {}", e.what());
        return {{"status", "error"}, {"message", "Internal server error", "details", e.what()}};
    }
}

json JsonRpcServer::handle_get_status(const json& params) {
    (void)params;
    json result;
    result["node_id"] = node_app_.status_.node_id;
    result["p2p_address"] = node_app_.status_.p2p_address;
    result["rpc_address"] = node_app_.status_.rpc_address;
    result["current_block_height"] = node_app_.next_block_height_ - 1;
    result["last_block_hash"] = chrono_util::bytes_to_hex(node_app_.last_block_hash_);
    result["connected_peers_count"] = node_app_.status_.connected_peers.load();
    
    return {{"status", "success"}, {"result", result}};
}

json JsonRpcServer::handle_get_block(const json& params) {
    if (!params.contains("height") && !params.contains("hash")) {
        return {{"status", "error"}, {"message", "Missing 'height' or 'hash' parameter"}};
    }

    std::optional<chrono_ledger::Block> block;
    try {
        if (params.contains("height")) {
            uint64_t height = params["height"].get<uint64_t>();
            block = node_app_.getBlockchainStorage()->getBlock(height);
        } else {
            std::string hash_str = params["hash"].get<std::string>();
            chrono_util::Bytes hash = chrono_util::hex_to_bytes(hash_str);
            block = node_app_.getBlockchainStorage()->getBlock(hash);
        }
    } catch (const std::exception& e) {
        return {{"status", "error"}, {"message", "Invalid parameters", "details", e.what()}};
    }

    if (block) {
        return {{"status", "success"}, {"result", block->to_json()}};
    } else {
        return {{"status", "error"}, {"message", "Block not found"}};
    }
}

json JsonRpcServer::handle_get_transaction(const json& params) {
    if (!params.contains("hash")) {
        return {{"status", "error"}, {"message", "Missing 'hash' parameter"}};
    }
    
    std::string hash_str = params["hash"].get<std::string>();
    chrono_util::Bytes tx_hash;
    try {
        tx_hash = chrono_util::hex_to_bytes(hash_str);
    } catch (const std::exception& e) {
        return {{"status", "error"}, {"message", "Invalid hash format"}};
    }

    // 1. Search mempool (NodeApp's mempool, not local one)
    // Note: We need access to NodeApp's mempool. 
    // Assuming node_app_ has a public method or friend access.
    // For now, we can't easily access NodeApp's mempool without adding a getter to NodeApp.
    // Let's assume we can't find it in mempool for now unless we add that getter.
    
    // 2. Search recent blocks
    uint64_t current_height = node_app_.next_block_height_ - 1;
    for (uint64_t h = current_height; h > 0 && h > current_height - 100; --h) {
        auto block = node_app_.getBlockchainStorage()->getBlock(h);
        if (block) {
            for (const auto& tx : block->transactions) {
                if (tx.get_hash() == tx_hash) {
                    return {{"status", "success"}, {"result", tx.to_json()}};
                }
            }
        }
    }

    return {{"status", "error"}, {"message", "Transaction not found"}};
}

json JsonRpcServer::handle_get_candidates(const json& params) {
    (void)params;
    try {
        auto candidates = state_.get_node_registry().get_candidates();
        json result = json::array();
        for (const auto& c : candidates) {
            result.push_back(c.to_json());
        }
        return {{"status", "success"}, {"result", result}};
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Error processing get_candidates: {}", e.what());
        return {{"status", "error"}, {"message", "Internal server error", "details", e.what()}};
    }
}

json JsonRpcServer::handle_get_peers(const json& params) {
    (void)params;
    try {
        json peers_json = json::array();
        
        // Access NodeApp's connected peers directly (friend class)
        std::lock_guard<std::mutex> lock(node_app_.peers_mutex_);
        for (const auto& [id, peer] : node_app_.connected_peers_) {
            json peer_obj;
            peer_obj["node_id"] = peer.node_id;
            peer_obj["address"] = peer.address;
            peer_obj["last_block_hash"] = chrono_util::bytes_to_hex(peer.last_block_hash);
            peer_obj["current_block_height"] = peer.current_block_height;
            peer_obj["score"] = peer.score;
            
            // Calculate last seen in seconds ago
            auto now = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - peer.last_seen_time).count();
            peer_obj["last_seen_seconds_ago"] = duration;
            
            peers_json.push_back(peer_obj);
        }
        
        return {{"status", "success"}, {"result", peers_json}};
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Error processing get_peers: {}", e.what());
        return {{"status", "error"}, {"message", "Internal server error", "details", e.what()}};
    }
}

json JsonRpcServer::handle_get_mempool(const json& params) {
    (void)params;
    try {
        auto mempool = node_app_.get_mempool_const();
        json result = json::array();
        for (const auto& tx : mempool) {
            result.push_back(tx.to_json());
        }
        return {{"status", "success"}, {"result", result}};
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Error processing get_mempool: {}", e.what());
        return {{"status", "error"}, {"message", "Internal server error", "details", e.what()}};
    }
}

// --- Mempool Access (Internal use, not direct RPC methods) ---
/**
 * @brief Returns the current list of pending transactions in the mempool.
 * @return A `std::vector` of `chrono_ledger::Transaction` objects currently in the mempool.
 */
std::vector<chrono_ledger::Transaction> JsonRpcServer::get_mempool() const {
    // Note: This mempool is internal to JsonRpcServer, distinct from NodeApp's mempool.
    // This distinction might need to be clarified or refactored in the future.
    return mempool_;
}

/**
 * @brief Clears all transactions from the mempool.
 *
 * This method is typically called after transactions have been successfully
 * included in a new block and are no longer pending.
 */
void JsonRpcServer::clear_mempool() {
    mempool_.clear();
}

/**
 * @brief Adds a transaction to the mempool.
 *
 * This method allows external components (e.g., P2P layer) to add transactions
 * to the RPC server's mempool for processing.
 *
 * @param tx The `chrono_ledger::Transaction` object to add to the mempool.
 */
void JsonRpcServer::add_transaction_to_mempool(const chrono_ledger::Transaction& tx) {
    mempool_.push_back(tx);
}

} // namespace chrono_node