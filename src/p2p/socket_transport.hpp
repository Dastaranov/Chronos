//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file socket_transport.hpp
 * @brief This file defines the SocketTransport class, an implementation of the ITransport interface using TCP sockets.
 *
 * The SocketTransport class provides a concrete network transport layer for the Chronos P2P system.
 * It leverages `P2pServer` for listening to incoming connections and `P2pClient` for establishing
 * outgoing connections. It manages active peer connections and facilitates message publishing
 * and handling through callbacks.
 *
 * Key functionalities include:
 * - `SocketTransport()`: Constructor to initialize the transport.
 * - `~SocketTransport()`: Destructor to clean up resources.
 * - `listen(const std::string& addr, int port)`: Starts listening for incoming connections.
 * - `connect(const std::string& host, int port)`: Connects to a remote peer.
 * - `publish(const std::string& topic, const Bytes& msg)`: Publishes a message to connected peers.
 * - `on_message(MsgHandler cb)`: Sets the callback for handling incoming messages.
 * - `handle_incoming_connection(int client_socket, const std::string& message)`: Internal handler for messages from new connections.
 */

#pragma once

#include "p2p/transport.hpp"
#include "p2p/p2p_server.hpp"
#include "p2p/p2p_client.hpp"
#include "p2p/peer_manager.hpp"

#include "util/log.hpp"

#include <memory>
#include <unordered_map>
#include <mutex> // For std::mutex
#include <thread> // For std::thread
#include <atomic> // For std::atomic

namespace chrono_p2p {

/**
 * @class SocketTransport
 * @brief Implements the ITransport interface using TCP sockets for P2P communication.
 *
 * This class provides the concrete implementation for network communication in the Chronos
 * P2P layer. It uses `P2pServer` to accept incoming connections and `P2pClient` to initiate
 * outgoing connections. It also manages a list of active client connections and dispatches
 * incoming messages to a registered callback.
 */
class SocketTransport : public ITransport {
public:
    using MsgHandler = std::function<void(const std::string& topic, const Bytes& data, const std::string& sender_id)>;

    /**
     * @brief Constructs a SocketTransport object.
     *
     * Initializes the internal components, including the `PeerManager` and prepares
     * for setting up the `P2pServer`.
     * 
     * @param status A reference to the NodeStatus object to update connected peer count.
     */
    explicit SocketTransport(chrono_node::NodeStatus& status);

    /**
     * @brief Destroys the SocketTransport object.
     *
     * Cleans up any active server or client connections and releases resources.
     */
    ~SocketTransport() override;

    /**
     * @brief Starts listening for incoming P2P connections.
     *
     * This method initializes and starts the internal `P2pServer` on the specified
     * address and port. It also sets up the server's message handler to process
     * incoming messages from new connections.
     *
     * @param addr The address to bind the server to (e.g., "0.0.0.0").
     * @param port The port number to listen on.
     * @return `true` if the server started listening successfully, `false` otherwise.
     */
    bool listen(const std::string& addr, int port) override;

    /**
     * @brief Connects to a remote peer.
     *
     * This method establishes an outgoing connection to a peer at the given host and port.
     * It creates a `P2pClient` instance for this connection and adds it to the list of
     * active clients.
     *
     * @param host The hostname or IP address of the peer.
     * @param port The port number of the peer.
     * @return `true` if the connection was successful, `false` otherwise.
     */
    bool connect(const std::string& host, int port) override;

    /**
     * @brief Publishes a message to all connected peers.
     *
     * This method iterates through all active client connections and sends the provided
     * message (`topic` and `msg`) to each peer.
     *
     * @param topic The topic of the message.
     * @param msg The message data as a `Bytes` object.
     * @return `true` if the message was published to at least one peer, `false` otherwise.
     */
    bool publish(const std::string& topic, const Bytes& msg) override;

    /**
     * @brief Sets the callback function for handling incoming messages.
     *
     * This method registers a `MsgHandler` callback that will be invoked whenever
     * a message is received from any connected peer.
     *
     * @param cb The `MsgHandler` function to be called.
     */
    void on_message(MsgHandler cb) override;

private:
    std::unique_ptr<P2pServer> p2p_server_; ///< @var p2p_server_ The P2P server instance for accepting incoming connections.
    PeerManager peer_manager_; ///< @var peer_manager_ Manages the list of known peers.
    MsgHandler message_callback_; ///< @var message_callback_ The callback function to be invoked for incoming messages.

    // Map to hold active client connections for sending messages
    // Key: "ip:port", Value: P2pClient instance
    std::unordered_map<std::string, std::unique_ptr<P2pClient>> active_clients_; ///< @var active_clients_ A map storing active outgoing client connections, keyed by "ip:port".
    std::mutex active_clients_mutex_; ///< @var active_clients_mutex_ A mutex to protect `active_clients_` from concurrent access.
    chrono_node::NodeStatus& status_; ///< @var status_ Reference to the node's status object for updating connected peer count.

    std::unordered_map<std::string, std::thread> client_receive_threads_; ///< @var client_receive_threads_ Threads dedicated to receiving messages from each active client.
    std::atomic<bool> stop_threads_ = false; ///< @var stop_threads_ Flag to signal client receive threads to stop.

    /**
     * @brief Handles messages received from incoming connections.
     *
     * This internal method is passed as a callback to the `P2pServer`. It receives
     * messages from newly accepted client sockets and dispatches them to the
     * `message_callback_` if it is set.
     *
     * @param client_socket The socket descriptor of the client that sent the message.
     * @param message The received message as a `std::string`.
     */
    void handle_incoming_connection(const std::string& sender_addr, const std::string& message);

    /**
     * @brief The main loop for a client's receive thread.
     *
     * This function continuously calls `P2pClient::receive_message()` for a specific client
     * and processes the received data. It runs in a separate thread for each active client.
     *
     * @param peer_address The "ip:port" string of the peer this client is connected to.
     * @param client A pointer to the `P2pClient` instance for this connection.
     */
    void client_receive_loop(const std::string& peer_address, P2pClient* client);
};

} // namespace chrono_p2p