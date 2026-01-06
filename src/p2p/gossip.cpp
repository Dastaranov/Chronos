//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file gossip.cpp
 * @brief This file implements the Gossip class, which provides a basic gossip protocol for peer-to-peer communication.
 *
 * The Gossip class acts as an intermediary between the application logic and the underlying
 * network transport. It enables nodes to subscribe to topics, publish messages, and handle
 * incoming messages through a registered handler. This implementation uses an `ITransport`
 * interface for network operations, allowing for flexible transport layer choices.
 *
 * Key functions implemented:
 * - `Gossip::Gossip`: Constructor to initialize the gossip protocol with a transport layer.
 * - `Gossip::subscribe`: Registers interest in a specific message topic.
 * - `Gossip::publish`: Sends a message to all peers subscribed to a given topic.
 * - `Gossip::set_message_handler`: Configures the callback for processing received messages.
 * - `Gossip::connect_to_peer`: Initiates a connection to another node.
 * - `Gossip::start_listening`: Binds the node to a network address to accept incoming connections.
 */

#include "p2p/gossip.hpp"
#include "util/log.hpp"
#include "p2p_messages.pb.h" // NEW
#include "google/protobuf/message.h" // NEW

namespace chrono_p2p {

/**
 * @brief Constructs a Gossip object with a specified transport layer.
 *
 * Initializes the gossip protocol handler by taking ownership of an `ITransport` implementation.
 * This transport will be used for all subsequent network communication operations, such as
 * connecting to peers, listening for connections, and sending/receiving messages.
 *
 * @param transport A unique pointer to an `ITransport` object. Ownership is transferred.
 */
Gossip::Gossip(std::unique_ptr<ITransport> transport)
    : transport_(std::move(transport)) {
    LOG_INFO(chrono_util::LogCategory::P2P, "Gossip initialized.");
}

/**
 * @brief Subscribes the node to a specific message topic.
 *
 * This method registers the node's interest in a particular topic. While the current
 * implementation only marks the topic as subscribed internally, a more advanced system
 * might communicate this subscription to peers.
 *
 * @param topic The name of the topic to subscribe to.
 */
void Gossip::subscribe(const std::string& topic) {
    subs_[topic] = true;
    LOG_INFO(chrono_util::LogCategory::P2P, "Subscribed to topic: {}", topic);
}

/**
 * @brief Publishes a message to a given topic for dissemination across the network.
 *
 * If an underlying transport is initialized, this method uses it to publish the message
 * to the specified topic. If no transport is available, a warning is logged.
 *
 * @param topic The name of the topic to publish the message to.
 * @param data The message data as a `Bytes` object.
 */
void Gossip::publish(const std::string& topic, const chrono_p2p::P2PMessage& message) {
    if (transport_) {
        // Serialize the Protobuf message to Bytes
        Bytes data(message.ByteSizeLong());
        if (!message.SerializeToArray(data.data(), data.size())) {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to serialize Protobuf P2PMessage.");
            return;
        }
        transport_->publish(topic, data);
        LOG_INFO(chrono_util::LogCategory::P2P, "Published Protobuf message to topic: {}", topic);
    } else {
        LOG_WARN(chrono_util::LogCategory::P2P, "Cannot publish, transport not initialized.");
    }
}

void Gossip::send_direct(const std::string& peer_id, const chrono_p2p::P2PMessage& message) {
    if (transport_) {
        // Serialize the Protobuf message to Bytes
        Bytes data(message.ByteSizeLong());
        if (!message.SerializeToArray(data.data(), data.size())) {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to serialize Protobuf P2PMessage for direct send.");
            return;
        }
        transport_->send_direct(peer_id, data);
        LOG_INFO(chrono_util::LogCategory::P2P, "Sent direct Protobuf message to {}", peer_id);
    } else {
        LOG_WARN(chrono_util::LogCategory::P2P, "Cannot send direct message, transport not initialized.");
    }
}

/**
 * @brief Sets a callback function to handle incoming messages.
 *
 * This method registers a `MsgHandler` function with the underlying transport.
 * This handler will be invoked by the transport whenever a message is received
 * from a peer, allowing the Gossip layer to process it.
 *
 * @param handler A `MsgHandler` function object that takes a topic string and message data as `Bytes`.
 */
void Gossip::set_message_handler(MsgHandler handler) {
    if (transport_) {
        transport_->on_message(std::move(handler));
    } else {
        LOG_WARN(chrono_util::LogCategory::P2P, "Cannot set message handler, transport not initialized.");
    }
}

/**
 * @brief Connects to a specified peer.
 *
 * This method delegates the connection request to the underlying transport layer.
 * It attempts to establish a network connection to another node at the given host and port.
 *
 * @param host The hostname or IP address of the peer to connect to.
 * @param port The port number of the peer.
 * @return `true` if the connection was successfully established, `false` otherwise.
 */
bool Gossip::connect_to_peer(const std::string& host, int port) {
    if (transport_) {
        return transport_->connect(host, port);
    }
    LOG_WARN(chrono_util::LogCategory::P2P, "Cannot connect to peer, transport not initialized.");
    return false;
}

/**
 * @brief Starts listening for incoming peer connections.
 *
 * This method instructs the underlying transport layer to bind to the specified
 * address and port, making the node available to accept incoming connections
 * from other peers in the network.
 *
 * @param addr The address to bind to (e.g., "0.0.0.0" for all available network interfaces).
 * @param port The port number to listen on for incoming connections.
 * @return `true` if listening started successfully, `false` otherwise.
 */
bool Gossip::start_listening(const std::string& addr, int port) {
    if (transport_) {
        return transport_->listen(addr, port);
    }
    LOG_WARN(chrono_util::LogCategory::P2P, "Cannot start listening, transport not initialized.");
    return false;
}

} // namespace chrono_p2p