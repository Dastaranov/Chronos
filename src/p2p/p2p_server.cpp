//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file p2p_server.cpp
 * @brief This file implements the P2pServer class, which provides basic server-side P2P networking functionalities.
 *
 * The P2pServer class implementation includes methods for setting up a listening socket,
 * accepting incoming client connections, and handling communication with each client in a
 * separate thread. It dispatches received messages to a user-defined handler.
 *
 * Key functions implemented:
 * - `P2pServer::P2pServer`: Constructor to initialize the server.
 * - `P2pServer::~P2pServer`: Destructor to clean up server resources.
 * - `P2pServer::start()`: Initiates the server's listening thread.
 * - `P2pServer::stop()`: Shuts down the server.
 * - `P2pServer::run_server()`: The main server loop for accepting connections.
 * - `P2pServer::handle_client()`: Manages communication with an individual client.
 */

#include "p2p/p2p_server.hpp"
#include <arpa/inet.h> // For inet_ntoa
#include <cstring> // For memset

namespace chrono_p2p {

/**
 * @brief Constructs a P2pServer object.
 *
 * Initializes the server with the specified listening `port` and a `MessageHandler`
 * callback function. The `server_fd_` is set to -1 (no socket yet), and `running_`
 * is set to false, indicating the server is not yet active.
 *
 * @param port The port number on which the server will listen for incoming connections.
 * @param handler The `MessageHandler` function to be called when a message is received from a client.
 */
P2pServer::P2pServer(int port, MessageHandler handler, chrono_node::NodeStatus& status)
    : port_(port), server_fd_(-1), message_handler_(std::move(handler)), running_(false), status_(status) {}

/**
 * @brief Destroys the P2pServer object.
 *
 * Ensures that the server is properly stopped by calling `stop()` when the object
 * is destroyed. This cleans up any open sockets and joins the server thread.
 */
P2pServer::~P2pServer() {
    stop();
}

/**
 * @brief Starts the P2P server.
 *
 * If the server is not already running, this method sets the `running_` flag to true
 * and launches a new thread (`server_thread_`) to execute the `run_server()` method.
 * This allows the server to begin listening for and accepting client connections.
 */
bool P2pServer::start() {
    if (running_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "P2P server already running.");
        return true; // Already running is not a failure to start
    }

    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    // Creating socket file descriptor
    if ((server_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Socket creation failed on port {}: {} (errno={})", port_, strerror(errno), errno);
        return false;
    }

    // Forcefully attaching socket to the port, allowing reuse of address/port
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: setsockopt failed on port {}: {} (errno={})", port_, strerror(errno), errno);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // Listen on all available network interfaces
    address.sin_port = htons(port_);

    // Forcefully attaching socket to the port
    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Bind failed on port {}", port_);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    if (listen(server_fd_, 10) < 0) { // Max 10 pending connections in the queue
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Listen failed on port {}", port_);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    server_thread_ = std::thread(&P2pServer::run_server, this);
    LOG_INFO(chrono_util::LogCategory::P2P, "P2P server started and listening on port {}", port_);
    return true;
}

/**
 * @brief Stops the P2P server.
 *
 * This method sets the `running_` flag to false, which signals the `run_server()`
 * loop to terminate. It then closes the `server_fd_` (listening socket) and
 * joins the `server_thread_` to ensure a clean shutdown and prevent resource leaks.
 */
void P2pServer::stop() {
    if (!running_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "P2P server not running.");
        return;
    }

    running_ = false;
    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    LOG_INFO(chrono_util::LogCategory::P2P, "P2P server stopped.");
}

void P2pServer::run_server() {
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    while (running_) {
        int client_socket;
        if ((client_socket = accept(server_fd_, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            int err = errno;
            if (err == EWOULDBLOCK || err == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (running_) { // Only log error if server is still supposed to be running
                LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Accept failed on port {}: {} (errno={})", port_, strerror(err), err);
            }
            continue;
        }
        LOG_INFO(chrono_util::LogCategory::P2P, "P2pServer: Accepted new connection on port {} from {}:{}", port_, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        status_.connected_peers++; // Increment connected peers count for incoming connection
        
        // Get client IP and port for sender_addr
        struct sockaddr_in client_addr_info;
        socklen_t client_addr_len = sizeof(client_addr_info);
        getpeername(client_socket, (struct sockaddr*)&client_addr_info, &client_addr_len);
        std::string client_ip = inet_ntoa(client_addr_info.sin_addr);
        int client_port = ntohs(client_addr_info.sin_port);
        std::string client_address = client_ip + ":" + std::to_string(client_port);

        std::thread client_handler(&P2pServer::handle_client, this, client_socket, client_address);
        client_handler.detach();
    }
    LOG_INFO(chrono_util::LogCategory::P2P, "P2pServer: run_server loop terminated for port {}", port_);
}

/**
 * @brief Handles communication with an individual connected client.
 *
 * This method runs in a separate thread for each client. It continuously reads
 * data from the client's socket into a buffer. Once a message is received, it
 * calls the `message_handler_` to process the message. After communication
 * (or on error/disconnection), the client socket is closed.
 *
 * @param client_socket The socket file descriptor for the connected client.
 */
bool P2pServer::send_n_bytes(int socket_fd, const void* buffer, size_t n_bytes) {
    size_t total_sent = 0;
    while (total_sent < n_bytes) {
        ssize_t sent = send(socket_fd, static_cast<const char*>(buffer) + total_sent, n_bytes - total_sent, 0);
        if (sent < 0) {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send {} bytes to socket {}: {} (errno={})", n_bytes, socket_fd, strerror(errno), errno);
            return false;
        }
        total_sent += sent;
    }
    return true;
}

bool P2pServer::recv_n_bytes(int socket_fd, void* buffer, size_t n_bytes) {
    size_t total_received = 0;
    while (total_received < n_bytes) {
        ssize_t received = recv(socket_fd, static_cast<char*>(buffer) + total_received, n_bytes - total_received, 0);
        if (received <= 0) { // 0 indicates disconnection, <0 indicates error
            if (received == 0) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Client disconnected gracefully from socket {} while receiving {} bytes.", socket_fd, n_bytes);
            } else {
                LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to receive {} bytes from socket {}: {} (errno={})", n_bytes, socket_fd, strerror(errno), errno);
            }
            return false;
        }
        total_received += received;
    }
    return true;
}

void P2pServer::handle_client(int client_socket, const std::string& client_address) {
    while (running_) {
        uint32_t message_size;
        if (!recv_n_bytes(client_socket, &message_size, sizeof(message_size))) {
            LOG_WARN(chrono_util::LogCategory::P2P, "Failed to receive message size from client {} ({}). Peer may have disconnected.", client_socket, client_address);
            break; // Client disconnected or error
        }

        std::string received_message_data(message_size, '\0'); // Allocate string for message data
        if (!recv_n_bytes(client_socket, received_message_data.data(), message_size)) {
            LOG_WARN(chrono_util::LogCategory::P2P, "Failed to receive message data from client {} ({}). Peer may have disconnected.", client_socket, client_address);
            break; // Client disconnected or error
        }
        
        message_handler_(client_address, received_message_data);
    }

    close(client_socket); // Close the client socket when the loop breaks (client disconnected or error)
    status_.connected_peers--; // Decrement connected peers count on disconnection
    LOG_INFO(chrono_util::LogCategory::P2P, "Client handler for {} terminated.", client_address);
}

} // namespace chrono_p2p