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


#include "crypto/aes_crypto.hpp"
#include "crypto/kyber_crypto.hpp"
#include "crypto/blake3.hpp"

namespace chrono_p2p {

/**
 * @class P2pServer
 * @brief Provides server-side functionalities for listening to and handling P2P connections with Kyber encryption.
 */
class P2pServer {
public:
    using MessageHandler = std::function<void(const std::string& sender_addr, const std::string& message)>;

    P2pServer(int port, MessageHandler handler, chrono_node::NodeStatus& status);
    ~P2pServer();

    bool start();
    void stop();

private:
    int port_;
    int server_fd_;
    MessageHandler message_handler_;
    std::thread server_thread_;
    bool running_;
    chrono_node::NodeStatus& status_;

    bool send_n_bytes(int socket_fd, const void* buffer, size_t n_bytes);
    bool recv_n_bytes(int socket_fd, void* buffer, size_t n_bytes);

    void run_server();
    void handle_client(int client_socket, const std::string& client_address, const std::string& client_ip);
    
    // Handshake
    bool perform_handshake(int client_socket, chrono_util::Bytes& session_key);

    // IP Limiting
    std::unordered_map<std::string, int> ip_counts_;
    std::mutex ip_counts_mutex_;
    const int MAX_PEERS_PER_IP = 2; // Allow max 2 connections per IP (e.g. 1 full node + 1 light client or similar)
};

} // namespace chrono_p2p