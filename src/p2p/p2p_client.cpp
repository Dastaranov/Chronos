//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file p2p_client.cpp
 * @brief This file implements the P2pClient class, providing basic client-side P2P networking functionalities.
 *
 * The P2pClient class encapsulates the logic for establishing and managing a client-side
 * connection to a peer in a peer-to-peer network. It provides methods for connecting to a peer,
 * sending messages, receiving messages, and disconnecting. This implementation uses standard
 * Berkeley sockets for network communication.
 *
 * Key functions implemented:
 * - `P2pClient::P2pClient()`: Constructor to initialize the client.
 * - `P2pClient::~P2pClient()`: Destructor to clean up socket resources.
 * - `P2pClient::connect_to_peer()`: Establishes a connection to a remote peer.
 * - `P2pClient::send_message()`: Sends a string message to the connected peer.
 * - `P2pClient::receive_message()`: Receives a string message from the connected peer.
 * - `P2pClient::disconnect()`: Closes the connection to the peer.
 */

#include "p2p/p2p_client.hpp"
#include <cstring> // For memset
#include <fcntl.h> // For fcntl
#include <vector> // For Bytes
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::milliseconds

namespace chrono_p2p {

P2pClient::P2pClient() : sock_(-1), connected_(false) {}

P2pClient::~P2pClient() {
    disconnect();
}

bool P2pClient::connect_to_peer(const std::string& ip_address, int port) {
    if (connected_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Already connected to a peer. Disconnect first.");
        return false;
    }

    sock_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_ == -1) {
        LOG_ERROR(chrono_util::LogCategory::P2P,
            "Socket creation failed: {} (errno={})",
            strerror(errno),
            errno
        );
        return false;
    }

    struct sockaddr_in server;
    server.sin_addr.s_addr = inet_addr(ip_address.c_str());
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    if (connect(sock_, (struct sockaddr *)&server, sizeof(server)) < 0) {
        LOG_ERROR(chrono_util::LogCategory::P2P,
            "Connect to {}:{} failed: {} (errno={})",
            ip_address,
            port,
            strerror(errno),
            errno
        );
        close(sock_);
        sock_ = -1;
        return false;
    }

    connected_ = true;
    peer_address_ = ip_address + ":" + std::to_string(port); // NEW
    LOG_INFO(chrono_util::LogCategory::P2P, "Connected to peer {}:{}", ip_address, port);
    return true;
}

bool P2pClient::send_n_bytes(const void* buffer, size_t n_bytes) {
    size_t total_sent = 0;
    while (total_sent < n_bytes) {
        ssize_t sent = send(sock_, static_cast<const char*>(buffer) + total_sent, n_bytes - total_sent, 0);
        if (sent < 0) {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send {} bytes to {}: {} (errno={})", n_bytes, peer_address_, strerror(errno), errno);
            return false;
        }
        total_sent += sent;
    }
    return true;
}

bool P2pClient::recv_n_bytes(void* buffer, size_t n_bytes) {
    size_t total_received = 0;
    while (total_received < n_bytes) {
        ssize_t received = recv(sock_, static_cast<char*>(buffer) + total_received, n_bytes - total_received, 0);
        if (received <= 0) { // 0 indicates disconnection, <0 indicates error
            if (received == 0) {
                LOG_INFO(chrono_util::LogCategory::P2P, "Peer {} disconnected gracefully while receiving {} bytes.", peer_address_, n_bytes);
            } else {
                LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to receive {} bytes from {}: {} (errno={})", n_bytes, peer_address_, strerror(errno), errno);
            }
            return false;
        }
        total_received += received;
    }
    return true;
}

bool P2pClient::send_message(const Bytes& message) {
    if (!connected_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Not connected to any peer. Cannot send message.");
        return false;
    }

    uint32_t message_size = message.size();
    if (!send_n_bytes(&message_size, sizeof(message_size))) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send message size to {}.", peer_address_);
        return false;
    }
    if (!send_n_bytes(message.data(), message_size)) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send message data to {}.", peer_address_);
        return false;
    }
    return true;
}

bool P2pClient::receive_message() {
    if (!connected_) {
        return false;
    }

    uint32_t message_size;
    if (!recv_n_bytes(&message_size, sizeof(message_size))) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Failed to receive message size from {}. Peer may have disconnected.", peer_address_);
        connected_ = false;
        return false;
    }

    Bytes received_data(message_size);
    if (!recv_n_bytes(received_data.data(), message_size)) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Failed to receive message data from {}. Peer may have disconnected.", peer_address_);
        connected_ = false;
        return false;
    }

    if (message_handler_) {
        // For P2pClient, we don't have a distinct topic per message, use a generic one.
        message_handler_("p2p", received_data, peer_address_);
    } else {
        LOG_WARN(chrono_util::LogCategory::P2P, "P2pClient: No message handler set for received message from {}.", peer_address_);
    }
    return connected_;
}

void P2pClient::disconnect() {
    if (connected_) {
        close(sock_);
        sock_ = -1;
        connected_ = false;
        LOG_INFO(chrono_util::LogCategory::P2P, "Disconnected from peer.");
    }
}

void P2pClient::set_message_handler(MsgHandler handler) {
    message_handler_ = std::move(handler);
}

} // namespace chrono_p2p