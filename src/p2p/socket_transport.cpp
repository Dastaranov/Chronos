//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file socket_transport.cpp
 * @brief This file implements the SocketTransport class, an implementation of the ITransport interface using TCP sockets.
 *
 * The SocketTransport class provides a concrete network transport layer for the Chronos P2P system.
 * It leverages `P2pServer` for listening to incoming connections and `P2pClient` for establishing
 * outgoing connections. It manages active peer connections and facilitates message publishing
 * and handling through callbacks. Messages are encapsulated in JSON format for topic-based routing.
 *
 * Key functions implemented:
 * - `SocketTransport::SocketTransport()`: Constructor to initialize the transport.
 * - `SocketTransport::~SocketTransport()`: Destructor to clean up resources.
 * - `SocketTransport::listen()`: Starts listening for incoming connections.
 * - `SocketTransport::connect()`: Connects to a remote peer.
 * - `SocketTransport::publish()`: Publishes a message to connected peers.
 * - `SocketTransport::on_message()`: Sets the callback for handling incoming messages.
 * - `SocketTransport::handle_incoming_connection()`: Internal handler for messages from new connections.
 */

#include "p2p/socket_transport.hpp"
#include <arpa/inet.h> // For inet_ntoa
#include "util/bytes.hpp" // For bytes_to_hex and hex_to_bytes
#include <chrono> // For std::chrono::milliseconds

namespace chrono_p2p {

SocketTransport::SocketTransport(chrono_node::NodeStatus& status) : p2p_server_(nullptr), status_(status), stop_threads_(false) {}

SocketTransport::~SocketTransport() {
    stop_threads_ = true; // Signal all threads to stop
    if (p2p_server_) {
        p2p_server_->stop();
    }

    // Join all client receive threads
    for (auto& pair : client_receive_threads_) {
        if (pair.second.joinable()) {
            pair.second.join();
        }
    }
}

bool SocketTransport::listen(const std::string& addr, int port) {
    (void)addr; // Suppress unused parameter warning

    p2p_server_ = std::make_unique<P2pServer>(port, [this](const std::string& sender_addr, const std::string& message) { // NEW signature
        this->handle_incoming_connection(sender_addr, message); // NEW invocation
    }, status_);
    if (p2p_server_->start()) {
        LOG_INFO(chrono_util::LogCategory::P2P, "SocketTransport: Started listening on port {}", port);
        return true;
    } else {
        LOG_ERROR(chrono_util::LogCategory::P2P, "SocketTransport: Failed to start listening on port {}", port);
        p2p_server_.reset(); // Clear the server if it failed to start
        return false;
    }
}

bool SocketTransport::connect(const std::string& host, int port) {
    std::string peer_address = host + ":" + std::to_string(port);
    std::lock_guard<std::mutex> lock(active_clients_mutex_);

    if (active_clients_.count(peer_address)) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Already connected to peer {}. Skipping.", peer_address);
        return true;
    }

    auto client = std::make_unique<P2pClient>();
    if (client->connect_to_peer(host, port)) {
        // Set the message handler for this client
        client->set_message_handler([this](const std::string& topic, const Bytes& data, const std::string& sender_id) { // NEW signature
            // The topic is now passed directly from P2pClient::receive_message.
            // The message data is already Bytes.
            if (message_callback_) {
                message_callback_(topic, data, sender_id); // Pass all parameters directly
            } else {
                LOG_WARN(chrono_util::LogCategory::P2P, "SocketTransport: No message callback set for received message from {}.", sender_id);
            }
        });

        peer_manager_.add_peer(peer_address);
        P2pClient* raw_client_ptr = client.get(); // Get raw pointer before moving unique_ptr
        active_clients_[peer_address] = std::move(client);
        status_.connected_peers++;
        LOG_INFO(chrono_util::LogCategory::P2P, "SocketTransport: Successfully connected to peer {}", peer_address);

        // Start a new thread to continuously receive messages from this client
        client_receive_threads_[peer_address] = std::thread(&SocketTransport::client_receive_loop, this, peer_address, raw_client_ptr);
        return true;
    } else {
        LOG_ERROR(chrono_util::LogCategory::P2P, "SocketTransport: Failed to connect to peer {}", peer_address);
        return false;
    }
}

bool SocketTransport::publish(const std::string& topic, const Bytes& msg) {
    std::lock_guard<std::mutex> lock(active_clients_mutex_);
    if (active_clients_.empty()) {
        LOG_WARN(chrono_util::LogCategory::P2P, "No active peers to publish to.");
        return false;
    }

    bool sent_to_any = false;
    for (auto const& [peer_addr, client] : active_clients_) {
        if (client->send_message(msg)) { // Pass raw Protobuf bytes directly
            LOG_INFO(chrono_util::LogCategory::P2P, "Published message to {} on topic {}", peer_addr, topic);
            sent_to_any = true;
        } else {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to publish message to {} on topic {}", peer_addr, topic);
            // In a real system, consider removing this peer if send consistently fails
        }
    }
    return sent_to_any;
}

bool SocketTransport::send_direct(const std::string& peer_id, const Bytes& msg) {
    std::lock_guard<std::mutex> lock(active_clients_mutex_);
    auto it = active_clients_.find(peer_id);
    if (it != active_clients_.end()) {
        if (it->second->send_message(msg)) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Sent direct message to {}", peer_id);
            return true;
        } else {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send direct message to {}", peer_id);
            return false;
        }
    }
    LOG_WARN(chrono_util::LogCategory::P2P, "Peer {} not found in active clients for direct message.", peer_id);
    return false;
}

void SocketTransport::on_message(MsgHandler cb) {
    message_callback_ = std::move(cb);
}

void SocketTransport::handle_incoming_connection(const std::string& sender_addr, const std::string& message) {
    if (!message_callback_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "No message callback set for incoming P2P message.");
        return;
    }

    // The message is now expected to be raw Protobuf bytes.
    // The topic is implicitly derived from the P2PMessage payload itself.
    // For now, we pass a generic "p2p" topic. The P2PMessage structure includes the type.
    Bytes data(message.begin(), message.end()); // Convert std::string to Bytes
    message_callback_("p2p", data, sender_addr); // NEW: Pass "p2p" as topic, and sender_addr
}

void SocketTransport::client_receive_loop(const std::string& peer_address, P2pClient* client) {
    LOG_INFO(chrono_util::LogCategory::P2P, "Starting receive loop for client {}", peer_address);
    while (!stop_threads_ && client->is_connected()) {
        if (!client->receive_message()) {
            // If receive_message returns false, it means the peer disconnected or an error occurred
            LOG_INFO(chrono_util::LogCategory::P2P, "Client {} disconnected or error in receive loop.", peer_address);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // Prevent busy-waiting
    }

    // Clean up after the loop
    std::lock_guard<std::mutex> lock(active_clients_mutex_);
    if (active_clients_.count(peer_address)) {
        active_clients_.erase(peer_address);
        status_.connected_peers--;
        LOG_INFO(chrono_util::LogCategory::P2P, "Removed disconnected client {} from active_clients_.", peer_address);
    }
    LOG_INFO(chrono_util::LogCategory::P2P, "Receive loop for client {} terminated.", peer_address);
}

} // namespace chrono_p2p