//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file jsonrpc.hpp
 * @brief This file defines the JsonRpcServer class, which provides a JSON-RPC 2.0 server for the Chronos node.
 *
 * The JsonRpcServer class enables external clients to interact with the Chronos node
 * using a standardized JSON-RPC protocol over HTTP. It manages a collection of RPC methods,
 * dispatches incoming requests to appropriate handlers, and interacts with the ledger state
 * and a temporary mempool for transactions.
 *
 * Key functionalities include:
 * - `JsonRpcServer(chrono_ledger::State& state)`: Constructor to initialize the server with a reference to the ledger state.
 * - `add(const std::string& method, std::function<json(const json&)> fn)`: Registers a new RPC method and its handler.
 * - `serve(int port)`: Starts the HTTP server to listen for incoming RPC requests.
 * - `handle_send_transaction(const json& params)`: Handler for sending transactions.
 * - `handle_get_balance(const json& params)`: Handler for retrieving account balances.
 * - `get_mempool()`: Returns the current list of transactions in the mempool.
 * - `clear_mempool()`: Clears the mempool.
 * - `add_transaction_to_mempool(const chrono_ledger::Transaction& tx)`: Adds a transaction to the mempool.
 */

#pragma once
#include <string>
#include <functional>
#include <unordered_map>
#include <type_traits> // Explicitly include for C++ standard library features
#include <iterator>    // Explicitly include for C++ standard library features
#include <vector>      // Explicitly include for std::vector
#include <nlohmann/json.hpp>
#include <httplib.h>

using json = nlohmann::json; // NEW



#include "ledger/state.hpp"
#include "ledger/transaction.hpp"
#include <vector>

namespace chrono_node {
    class NodeApp; // Forward declaration
}

namespace chrono_node {

/**
 * @class JsonRpcServer
 * @brief Implements a JSON-RPC 2.0 server for the Chronos node.
 *
 * This class provides an interface for external applications to interact with the
 * Chronos blockchain node. It registers RPC methods, processes incoming JSON-RPC
 * requests, and dispatches them to appropriate internal handlers. It also manages
 * a temporary mempool for transactions awaiting inclusion in a block.
 */
class JsonRpcServer {
  httplib::Server http_server_;
  std::unordered_map<std::string, std::function<nlohmann::json(const nlohmann::json&)>> handlers_; ///< @var handlers_ A map storing registered RPC method names and their corresponding handler functions.
  chrono_ledger::State& state_; ///< @var state_ A reference to the global ledger state, allowing RPC methods to query and modify it.
  std::vector<chrono_ledger::Transaction> mempool_; ///< @var mempool_ A temporary storage for transactions that have been received but not yet included in a block.
public:
  /**
   * @brief Constructs a JsonRpcServer object.
   *
   * Initializes the RPC server with a reference to the current ledger state.
   * It also registers the default set of RPC methods and their handlers.
   *
   * @param port The port number on which the RPC server will listen.
   * @param api_key Optional API key for authentication.
   * @param state A reference to the `chrono_ledger::State` object that this RPC server will interact with.
   * @param node_app A reference to the main `chrono_node::NodeApp` instance.
   */
  JsonRpcServer(int port, const std::string& api_key, chrono_ledger::State& state, chrono_node::NodeApp& node_app);

  /**
   * @brief Starts the HTTP server to listen for incoming RPC requests.
   *
   * This method binds the server to the specified `port` and begins listening
   * for HTTP requests. It processes incoming requests as JSON-RPC 2.0 calls
   * and dispatches them to the registered handlers.
   *
   * @param port The port number on which the RPC server will listen.
   */
  void serve(); // http_stub

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
  void add(const std::string& method, std::function<nlohmann::json(const nlohmann::json&)> fn);

private:
  int port_; ///< @var port_ The port number on which the RPC server listens.
  std::string api_key_; ///< @var api_key_ The API key for authentication.
  chrono_node::NodeApp& node_app_; ///< @var node_app_ A reference to the main NodeApp instance.

  // Handlers
  /**
   * @brief Handles the "get_status" RPC request.
   *
   * This method returns the current operational status of the node, including
   * block height, node ID, and connected peers.
   *
   * @param params A JSON object, typically empty for this method.
   * @return A JSON object containing the node's status information.
   */
  nlohmann::json handle_get_status(const nlohmann::json& params); // NEW: For sub-task 5.4 or 5.1 (specification)

  /**
   * @brief Handles the "get_block" RPC request.
   *
   * This method retrieves a block by its height or hash from the blockchain storage.
   *
   * @param params A JSON object containing either "height" (uint64) or "hash" (hex string).
   * @return A JSON object representing the block, or an error if not found/invalid params.
   */
  nlohmann::json handle_get_block(const nlohmann::json& params); // NEW

  /**
   * @brief Handles the "get_transaction" RPC request.
   *
   * This method retrieves a transaction by its hash from the mempool or blockchain history.
   *
   * @param params A JSON object containing "hash" (hex string) of the transaction.
   * @return A JSON object representing the transaction, or an error if not found/invalid params.
   */
  nlohmann::json handle_get_transaction(const nlohmann::json& params); // NEW

  /**
   * @brief Handles the "get_nonce" RPC request.
   *
   * This method retrieves the current nonce for a specific address from the ledger state.
   * This is essential for creating valid transactions.
   *
   * @param params A JSON object containing the "address" parameter.
   * @return A JSON object containing the nonce or an error message.
   */
  nlohmann::json handle_get_nonce(const nlohmann::json& params);

  /**
   * @brief Handles the "send_transaction" RPC request.
   *
   * This method is responsible for processing requests to submit new transactions
   * to the blockchain. It typically involves parsing transaction data from the
   * JSON parameters, validating it, and adding it to the mempool.
   *
   * @param params A JSON object containing the parameters for the transaction.
   * @return A JSON object representing the result of the operation (e.g., success/failure).
   */
  nlohmann::json handle_send_transaction(const nlohmann::json& params);

  /**
   * @brief Handles the "get_balance" RPC request.
   *
   * This method processes requests to retrieve the current balance of a specific
   * address from the ledger state.
   *
   * @param params A JSON object containing the parameters, typically including the "address".
   * @return A JSON object containing the balance or an error message.
   */
  nlohmann::json handle_get_balance(const nlohmann::json& params);

  /**
   * @brief Handles the "get_candidates" RPC request.
   *
   * This method retrieves the list of validator candidates and their voting status.
   *
   * @param params A JSON object (empty).
   * @return A JSON object containing the list of candidates.
   */
  nlohmann::json handle_get_candidates(const nlohmann::json& params);
  
  /**
   * @brief Handles the "get_peers" RPC request.
   *
   * This method retrieves the list of currently connected peers with their details.
   *
   * @param params A JSON object (empty).
   * @return A JSON object containing the list of peers.
   */
  nlohmann::json handle_get_peers(const nlohmann::json& params);

  /**
   * @brief Handles the "get_mempool" RPC request.
   *
   * This method retrieves the current list of transactions in the mempool.
   *
   * @param params A JSON object (empty).
   * @return A JSON object containing the list of transactions.
   */
  nlohmann::json handle_get_mempool(const nlohmann::json& params);

  // Mempool access
  /**
   * @brief Returns the current list of pending transactions in the mempool.
   *
   * @return A `std::vector` of `chrono_ledger::Transaction` objects currently in the mempool.
   */
  std::vector<chrono_ledger::Transaction> get_mempool() const;

  /**
   * @brief Clears all transactions from the mempool.
   *
   * This method is typically called after transactions have been successfully
   * included in a new block and are no longer pending.
   */
  void clear_mempool();

  /**
   * @brief Adds a transaction to the mempool.
   *
   * This method allows external components (e.g., P2P layer) to add transactions
   * to the RPC server's mempool for processing.
   *
   * @param tx The `chrono_ledger::Transaction` object to add to the mempool.
   */
  void add_transaction_to_mempool(const chrono_ledger::Transaction& tx);
};

} // namespace chrono_node