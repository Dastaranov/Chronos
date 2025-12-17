//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file p2p_server.hpp
 * @brief This file defines the P2pServer class, which provides basic server-side P2P networking functionalities.
 *
 * The P2pServer class encapsulates the logic for setting up and managing a server that
 * listens for incoming connections from other peers in a peer-to-peer network. It handles
 * accepting new connections, managing client communication in separate threads, and
 * dispatching received messages to a registered handler.
 *
 * Key functionalities include:
 * - `P2pServer(int port, MessageHandler handler)`: Constructor to initialize the server with a listening port and message handler.
 * - `~P2pServer()`: Destructor to clean up server resources and stop the server thread.
 * - `start()`: Initiates the server to begin listening for connections.
 * - `stop()`: Shuts down the server and its associated threads.
 * - `run_server()`: The main loop for the server thread, accepting new client connections.
 * - `handle_client(int client_socket)`: Manages communication with an individual connected client.
 */

#pragma once

#include <string>
#include <vector>
#include <thread>
#include <functional>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "util/log.hpp"
#include "node/node_status.hpp" // Include NodeStatus

namespace chrono_p2p {

/**
 * @class P2pServer
 * @brief Provides server-side functionalities for listening to and handling P2P connections.
 *
 * This class sets up a TCP server that listens on a specified port for incoming peer connections.
 * It manages accepted client connections, reads messages from them, and dispatches these messages
 * to a user-defined handler. Each client connection is handled in its own thread.
 */
class P2pServer {
public:
    /**
     * @brief Type alias for a message handler function.
     *
     * This function type defines the signature for callbacks that process messages received
     * from connected clients. It takes the client's socket descriptor and the received message string.
     */
    using MessageHandler = std::function<void(const std::string& sender_addr, const std::string& message)>;

    /**
     * @brief Constructs a P2pServer object.
     *
     * Initializes the server with the port it should listen on and a callback function
     * to handle messages received from connected clients.
     *
     * @param port The port number on which the server will listen for incoming connections.
     * @param handler The `MessageHandler` function to be called when a message is received from a client.
     * @param status A reference to the NodeStatus object to update connected peer count.
     */
    P2pServer(int port, MessageHandler handler, chrono_node::NodeStatus& status);

    /**
     * @brief Destroys the P2pServer object.
     *
     * Ensures that the server is properly stopped and all associated resources (sockets, threads)
     * are cleaned up when the server object is destroyed.
     */
    ~P2pServer();

    /**
     * @brief Starts the P2P server.
     *
     * This method initiates the server's listening process in a separate thread, allowing it
     * to accept incoming client connections.
     */
    bool start();

    /**
     * @brief Stops the P2P server.
     *
     * This method signals the server thread to terminate, closes the listening socket,
     * and joins the server thread to ensure a clean shutdown.
     */
    void stop();

private:
    int port_; ///< @var port_ The port number the server is listening on.
    int server_fd_; ///< @var server_fd_ The file descriptor for the server's listening socket.
    MessageHandler message_handler_; ///< @var message_handler_ The callback function to process messages from clients.
    std::thread server_thread_; ///< @var server_thread_ The thread in which the server's main loop runs.
    bool running_; ///< @var running_ A flag indicating whether the server is currently running.
    chrono_node::NodeStatus& status_; ///< @var status_ Reference to the node's status object for updating connected peer count.

    // Helper methods for socket I/O
    // Helper to send exactly N bytes
    bool send_n_bytes(int socket_fd, const void* buffer, size_t n_bytes);
    // Helper to receive exactly N bytes
    bool recv_n_bytes(int socket_fd, void* buffer, size_t n_bytes);

    /**
     * @brief The main loop for the server thread.
     *
     * This method continuously listens for and accepts new client connections.
     * Each accepted client connection is then passed to `handle_client` in a new thread.
     */
    void run_server();

    /**
     * @brief Handles communication with an individual connected client.
     *
     * This method runs in a separate thread for each client. It continuously reads
     * messages from the client's socket and dispatches them to the `message_handler_`.
     * It also handles client disconnections and read errors.
     *
     * @param client_socket The socket file descriptor for the connected client.
     */
    void handle_client(int client_socket, const std::string& client_address);
};

} // namespace chrono_p2p