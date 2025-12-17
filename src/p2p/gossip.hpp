//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file gossip.hpp
 * @brief This file defines the Gossip class, which implements a basic gossip protocol for peer-to-peer communication.
 *
 * The Gossip class provides a mechanism for nodes to disseminate information (messages) across a network
 * in a decentralized manner. It utilizes an underlying transport layer (`ITransport`) to send and receive
 * data. This class supports subscribing to topics, publishing messages, and handling incoming messages.
 *
 * Key functionalities include:
 * - `Gossip(std::unique_ptr<ITransport> transport)`: Constructor to initialize the gossip protocol with a transport layer.
 * - `subscribe(const std::string& topic)`: Subscribes the node to a specific message topic.
 * - `publish(const std::string& topic, const Bytes& data)`: Publishes a message to a given topic for dissemination.
 * - `set_message_handler(MsgHandler handler)`: Sets a callback function to handle incoming messages.
 * - `connect_to_peer(const std::string& host, int port)`: Connects to a specified peer.
 * - `start_listening(const std::string& addr, int port)`: Starts listening for incoming peer connections.
 */

#include "p2p_messages.pb.h" // NEW: Include generated Protobuf P2P messages

#pragma once
#include "p2p/transport.hpp"
#include <unordered_map>
#include <string>
#include <memory> // For std::unique_ptr

namespace chrono_p2p {

/**
 * @class Gossip
 * @brief Implements a basic gossip protocol for peer-to-peer communication.
 *
 * This class manages the dissemination of messages across a network of nodes.
 * It abstracts the underlying transport mechanism and provides an interface
 * for publishing and subscribing to message topics.
 */
class Gossip {
  std::unique_ptr<ITransport> transport_; ///< @var transport_ A unique pointer to the underlying transport layer used for sending and receiving data.
  std::unordered_map<std::string, bool> subs_; ///< @var subs_ A map storing the topics to which this node is subscribed. The boolean value indicates active subscription.
public:
  /**
   * @brief Constructs a Gossip object with a specified transport layer.
   *
   * Initializes the gossip protocol handler with an `ITransport` implementation,
   * which will be used for all network communication.
   *
   * @param transport A unique pointer to an `ITransport` object. The ownership is transferred to the Gossip instance.
   */
  Gossip(std::unique_ptr<ITransport> transport);

  /**
   * @brief Subscribes the node to a specific message topic.
   *
   * When subscribed to a topic, the node will receive messages published on that topic.
   *
   * @param topic The name of the topic to subscribe to.
   */
  void subscribe(const std::string& topic);

  /**
   * @brief Publishes a message to a given topic for dissemination across the network.
   *
   * This method sends the provided Protobuf message to all connected peers who are subscribed to the specified topic.
   * The message is serialized into bytes before transmission.
   *
   * @param topic The name of the topic to publish the message to.
   * @param message The Protobuf `P2PMessage` object to publish.
   */
  void publish(const std::string& topic, const chrono_p2p::P2PMessage& message);

  /**
   * @brief Sets a callback function to handle incoming messages.
   *
   * This handler will be invoked whenever a message is received from a peer.
   *
   * @param handler A `MsgHandler` function object that takes a topic string and message data as `Bytes`.
   */
  void set_message_handler(MsgHandler handler);

  /**
   * @brief Connects to a specified peer.
   *
   * Attempts to establish a connection to another node in the network at the given host and port.
   *
   * @param host The hostname or IP address of the peer.
   * @param port The port number of the peer.
   * @return `true` if the connection was successful, `false` otherwise.
   */
  bool connect_to_peer(const std::string& host, int port);

  /**
   * @brief Starts listening for incoming peer connections.
   *
   * Binds the transport layer to the specified address and port, making the node
   * discoverable and able to accept connections from other peers.
   *
   * @param addr The address to bind to (e.g., "0.0.0.0" for all interfaces).
   * @param port The port number to listen on.
   * @return `true` if listening started successfully, `false` otherwise.
   */
  bool start_listening(const std::string& addr, int port);
};

} // namespace chrono_p2p
