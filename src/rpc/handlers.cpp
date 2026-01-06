//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file handlers.cpp
 * @brief This file implements the JSON-RPC handlers for the Chronos node.
 *
 * This file contains the implementation of the `JsonRpcServer` class, which registers
 * and handles various RPC methods. It interacts with the ledger state and mempool
 * to process requests such as sending transactions and querying account balances.
 *
 * Key functions implemented:
 * - `JsonRpcServer::JsonRpcServer`: Constructor to register RPC methods.
 * - `JsonRpcServer::handle_send_transaction`: Processes requests to send new transactions.
 * - `JsonRpcServer::handle_get_balance`: Processes requests to retrieve account balances.
 * - `JsonRpcServer::get_mempool`: Returns the current list of pending transactions.
 * - `JsonRpcServer::clear_mempool`: Clears the list of pending transactions.
 * - `JsonRpcServer::add_transaction_to_mempool`: Adds a transaction to the mempool.
 */

#include "rpc/jsonrpc.hpp"
#include "ledger/state.hpp"
#include "address/address.hpp"
#include "crypto/signer.hpp"
#include "crypto/signer_hmac.hpp" // Include for SignerHMAC
#include "util/log.hpp"
#include <fmt/core.h> // NEW: For fmt::format

/**
 * @brief Constructs a JsonRpcServer object.
 *
 * Initializes the RPC server by registering the available RPC methods, such as
 * "send_transaction" and "get_balance". Each method is associated with a
 * corresponding handler function.
 *
 * @param state A reference to the `chrono_ledger::State` object, allowing RPC handlers
 *              to interact with the current ledger state.
 */
JsonRpcServer::JsonRpcServer(int port, chrono_ledger::State& state, chrono_node::NodeApp& node_app) 
    : http_server_(), handlers_(), state_(state), port_(port), node_app_(node_app) {
    add("send_transaction", [this](const json& params) {
        return handle_send_transaction(params);
    });
    add("get_balance", [this](const json& params) {
        return handle_get_balance(params);
    });
    // NEW: Register new handlers
    add("get_status", [this](const json& params) {
        return handle_get_status(params);
    });
    add("get_block", [this](const json& params) {
        return handle_get_block(params);
    });
    add("get_transaction", [this](const json& params) {
        return handle_get_transaction(params);
    });
}

/**
 * @brief Handles the "send_transaction" RPC request.
 *
 * This method processes incoming requests to send a new transaction.
 * Currently, it's a placeholder that logs the request and adds a dummy transaction
 * to the mempool. In a full implementation, it would parse the transaction details
 * from the `params`, validate the transaction, and then add it to the mempool.
 *
 * @param params A JSON object containing the parameters for the "send_transaction" method.
 * @return A JSON object indicating the status of the request (success or error).
 */
nlohmann::nlohmann::json JsonRpcServer::handle_send_transaction(const nlohmann::json& params) {
    // Validate parameters
    if (!params.contains("from_address") || !params.contains("to_address") ||
        !params.contains("amount") || !params.contains("signature") || !params.contains("public_key")) {
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32602}, {"message", "Missing one or more required parameters"}}}, {"id", nullptr}};
    }

    std::string from_address_str = params["from_address"].get<std::string>();
    std::string to_address_str = params["to_address"].get<std::string>();
    uint64_t amount = params["amount"].get<uint64_t>();
    std::string signature_hex = params["signature"].get<std::string>();
    std::string public_key_hex = params["public_key"].get<std::string>();

    try {
        chrono_address::Address from_addr = chrono_address::Address::from_string(from_address_str);
        chrono_address::Address to_addr = chrono_address::Address::from_string(to_address_str);
        chrono_crypto::Signature signature = chrono_crypto::Signature::from_hex(signature_hex);
        chrono_util::Bytes public_key = chrono_util::hex_to_bytes(public_key_hex);

        // Reconstruct transaction (assuming Transaction constructor takes these directly)
        chrono_ledger::Transaction tx(from_addr, to_addr, amount, 0, 0); // Placeholder for fee and nonce
        tx.signature = signature;
        tx.public_key = public_key;

        // Verify signature (simplified, typically done against raw transaction data)
        // For a real system, the transaction would need to generate a hash of its signable content.
        // For now, assume this is handled internally by tx.is_valid()
        if (!tx.is_valid()) {
             LOG_WARN(chrono_util::LogCategory::GENERAL, "Received invalid transaction via RPC from {}: {}", from_address_str, params.dump());
             return {{"jsonrpc", "2.0"}, {"error", {{"code", -32000}, {"message", "Invalid transaction signature or content"}}}, {"id", nullptr}};
        }

        // Add to NodeApp's mempool and publish
        if (node_app_.add_transaction_to_mempool(tx)) {
            node_app_.publish_transaction(tx);
            LOG_INFO(chrono_util::LogCategory::GENERAL, "Received and published transaction via RPC: {}", tx.to_string());
            return {{"jsonrpc", "2.0"}, {"result", {{"transaction_hash", bytes_to_hex(tx.get_hash())}, {"status", "pending"}}}, {"id", nullptr}};
        } else {
             LOG_WARN(chrono_util::LogCategory::GENERAL, "NodeApp rejected transaction via RPC: {}", tx.to_string());
             return {{"jsonrpc", "2.0"}, {"error", {{"code", -32000}, {"message", "Transaction rejected by node"}}}, {"id", nullptr}};
        }

    } catch (const std::exception& e) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Error processing send_transaction request: {}. Params: {}", e.what(), params.dump());
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32000}, {"message", fmt::format("Error processing transaction: {}", e.what())}}}, {"id", nullptr}};
    }
}

/**
 * @brief Handles the "get_balance" RPC request.
 *
 * This method processes incoming requests to retrieve the balance of a specific address.
 * It expects an "address" parameter in the JSON request. It queries the `state_` object
 * to get the balance and returns it in a JSON response.
 *
 * @param params A JSON object containing the parameters for the "get_balance" method,
 *               expected to include an "address" field.
 * @return A JSON object containing the balance of the requested address, or an error message if the address is missing.
 */
nlohmann::json JsonRpcServer::handle_get_balance(const nlohmann::json& params) {
    if (!params.contains("address")) {
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32602}, {"message", "Missing 'address' parameter"}}}, {"id", nullptr}};
    }
    std::string address_str = params["address"].get<std::string>();

    try {
        chrono_address::Address addr = chrono_address::Address::from_string(address_str);
        uint64_t balance = state_.get_balance(address_str); // state_.get_balance still takes string
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Received get_balance request for {}: {}", address_str, balance);
        return {{"jsonrpc", "2.0"}, {"result", {{"address", address_str}, {"balance", balance}}}, {"id", nullptr}};
    } catch (const std::exception& e) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Invalid address format in get_balance request: {}", address_str);
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32000}, {"message", fmt::format("Invalid address format: {}", e.what())}}}, {"id", nullptr}};
    }
}

nlohmann::json JsonRpcServer::handle_get_status(const nlohmann::json& params) {
    (void)params; // Suppress unused parameter warning
    json result;
    result["node_id"] = node_app_.status_.node_id;
    result["p2p_address"] = node_app_.status_.p2p_address;
    result["rpc_address"] = node_app_.status_.rpc_address;
    result["current_block_height"] = node_app_.next_block_height_ - 1; // current height is next_block_height_ - 1
    result["last_block_hash"] = chrono_util::bytes_to_hex(node_app_.last_block_hash_);
    result["connected_peers_count"] = node_app_.status_.connected_peers;
    result["is_syncing"] = node_app_.is_syncing();

    return {{"jsonrpc", "2.0"}, {"result", result}, {"id", nullptr}};
}

nlohmann::json JsonRpcServer::handle_get_block(const nlohmann::json& params) {
    if (!params.contains("height") && !params.contains("hash")) {
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32602}, {"message", "Missing 'height' or 'hash' parameter"}}}, {"id", nullptr}};
    }

    std::optional<chrono_ledger::Block> block;
    if (params.contains("height")) {
        uint64_t height = params["height"].get<uint64_t>();
        block = node_app_.getBlockchainStorage()->getBlock(height);
    } else { // params.contains("hash")
        std::string hash_str = params["hash"].get<std::string>();
        try {
            chrono_util::Bytes hash = chrono_util::hex_to_bytes(hash_str);
            block = node_app_.getBlockchainStorage()->getBlock(hash);
        } catch (const std::exception& e) {
            LOG_WARN(chrono_util::LogCategory::GENERAL, "Invalid hash format in get_block request: {}", hash_str);
            return {{"jsonrpc", "2.0"}, {"error", {{"code", -32000}, {"message", fmt::format("Invalid hash format: {}", e.what())}}}, {"id", nullptr}};
        }
    }

    if (block) {
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Retrieved block {} via RPC.", chrono_util::bytes_to_hex(block->get_header_hash()));
        return {{"jsonrpc", "2.0"}, {"result", block->to_json()}, {"id", nullptr}};
    } else {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Block not found for get_block request. Params: {}", params.dump());
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32001}, {"message", "Block not found"}}}, {"id", nullptr}};
    }
}

nlohmann::json JsonRpcServer::handle_get_transaction(const nlohmann::json& params) {
    if (!params.contains("hash")) {
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32602}, {"message", "Missing 'hash' parameter"}}}, {"id", nullptr}};
    }
    std::string hash_str = params["hash"].get<std::string>();

    chrono_util::Bytes tx_hash;
    try {
        tx_hash = chrono_util::hex_to_bytes(hash_str);
    } catch (const std::exception& e) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "Invalid hash format in get_transaction request: {}", hash_str);
        return {{"jsonrpc", "2.0"}, {"error", {{"code", -32000}, {"message", fmt::format("Invalid hash format: {}", e.what())}}}, {"id", nullptr}};
    }

    // 1. Search mempool
    for (const auto& tx : node_app_.get_mempool_const()) { // node_app_ needs a get_mempool_const() accessor
        if (tx.get_hash() == tx_hash) {
            LOG_INFO(chrono_util::LogCategory::GENERAL, "Retrieved transaction {} from mempool via RPC.", hash_str);
            return {{"jsonrpc", "2.0"}, {"result", tx.to_json()}, {"id", nullptr}};
        }
    }

    // 2. Search recent blocks (very inefficient, requires block scan or transaction index)
    // For now, iterate last few blocks
    // TODO: Implement proper transaction indexing in IBlockchainStorage
    uint64_t current_height = node_app_.next_block_height_ - 1;
    for (uint64_t h = current_height; h >= 0 && h > current_height - 100; --h) { // Search last 100 blocks
        std::optional<chrono_ledger::Block> block = node_app_.getBlockchainStorage()->getBlock(h);
        if (block) {
            for (const auto& tx : block->transactions) {
                if (tx.get_hash() == tx_hash) {
                    LOG_INFO(chrono_util::LogCategory::GENERAL, "Retrieved transaction {} from block {} via RPC.", hash_str, h);
                    return {{"jsonrpc", "2.0"}, {"result", tx.to_json()}, {"id", nullptr}};
                }
            }
        }
        if (h == 0) break; // Avoid underflow
    }

    LOG_WARN(chrono_util::LogCategory::GENERAL, "Transaction not found for get_transaction request. Hash: {}", hash_str);
    return {{"jsonrpc", "2.0"}, {"error", {{"code", -32001}, {"message", "Transaction not found"}}}, {"id", nullptr}};
}

/**
 * @brief Returns the current list of pending transactions in the mempool.
 *
 * This method provides access to the transactions that have been received but
 * not yet included in a block.
 *
 * @return A `std::vector` of `chrono_ledger::Transaction` objects currently in the mempool.
 */
std::vector<chrono_ledger::Transaction> JsonRpcServer::get_mempool() const {
    return mempool_;
}

/**
 * @brief Clears all transactions from the mempool.
 *
 * This method empties the list of pending transactions, typically after they
 * have been successfully included in a new block.
 */
void JsonRpcServer::clear_mempool() {
    mempool_.clear();
}

/**
 * @brief Adds a transaction to the mempool.
 *
 * This method is used to add a new transaction to the list of pending transactions.
 * In a real system, this would involve validation before adding.
 *
 * @param tx The `chrono_ledger::Transaction` object to add to the mempool.
 */
void JsonRpcServer::add_transaction_to_mempool(const chrono_ledger::Transaction& tx) {
    mempool_.push_back(tx);
}
