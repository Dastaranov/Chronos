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

namespace chrono_node { // Forward declaration for NodeApp

/**
 * @brief Constructs a JsonRpcServer object.
 *
 * Initializes the RPC server with a reference to the current ledger state and the
 * main node application instance. Method handlers are registered in the `serve()` method.
 *
 * @param port The port number on which the RPC server will listen.
 * @param state A reference to the `chrono_ledger::State` object that this RPC server will interact with.
 * @param node_app A reference to the main `chrono_node::NodeApp` instance.
 */
JsonRpcServer::JsonRpcServer(int port, chrono_ledger::State& state, chrono_node::NodeApp& node_app)
    : port_(port), state_(state), node_app_(node_app) {
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
    // NEW: Register other RPC handlers here

    http_server_.Post("/", [this](const httplib::Request& req, httplib::Response& res) {
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

    LOG_INFO(chrono_util::LogCategory::GENERAL, "JSON-RPC server listening on port {}", port_);
    if (!http_server_.listen("0.0.0.0", port_)) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to start HTTP server on port {}", port_);
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
        std::string recipient_pub_key_hex = params.at("recipient_pub_key").get<std::string>();
        uint64_t amount = params.at("amount").get<uint64_t>();
        uint64_t fee = params.at("fee").get<uint64_t>();
        uint64_t nonce = params.at("nonce").get<uint64_t>();
        std::string signature_hex = params.at("signature").get<std::string>();

        // Convert hex strings to Bytes and then to Address/Signature
        chrono_address::Address sender_addr(chrono_util::hex_to_bytes(sender_pub_key_hex));
        chrono_address::Address recipient_addr(chrono_util::hex_to_bytes(recipient_pub_key_hex));
        
        // Create transaction object without signature first
        chrono_ledger::Transaction tx(sender_addr, recipient_addr, amount, fee, nonce);
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