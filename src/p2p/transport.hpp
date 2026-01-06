//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file transport.hpp
 * @brief This file defines the ITransport interface, an abstract base class for network communication.
 *
 * The ITransport interface establishes a contract for any class that provides network
 * transport functionalities within the Chronos P2P system. It ensures a consistent API
 * for different underlying network implementations (e.g., TCP sockets, WebSockets, UDP).
 *
 * Key functionalities defined by this interface:
 * - `listen(const std::string& addr, int port)`: Starts listening for incoming connections.
 * - `connect(const std::string& host, int port)`: Connects to a remote peer.
 * - `publish(const std::string& topic, const Bytes& msg)`: Publishes a message to connected peers.
 * - `on_message(MsgHandler cb)`: Sets the callback for handling incoming messages.
 */

#pragma once
#include <string>
#include <functional>
#include <vector>
#include <cstdint>

using Bytes = std::vector<uint8_t>; ///< @brief Type alias for a byte vector, commonly used for raw data.
using MsgHandler = std::function<void(const std::string& topic, const Bytes&, const std::string& sender_id)>; ///< @brief Type alias for a message handler callback function.

/**
 * @class ITransport
 * @brief Abstract base class defining the interface for network communication.
 *
 * This interface provides a standardized way to interact with different network
 * transport mechanisms. Concrete implementations of this interface will handle
 * the specifics of establishing connections, sending, and receiving data.
 */
class ITransport {
public:
  /**
   * @brief Virtual destructor for the ITransport interface.
   *
   * Ensures proper cleanup of resources for derived classes when an ITransport pointer
   * is deleted.
   */
  virtual ~ITransport()=default;

  /**
   * @brief Pure virtual function to start listening for incoming connections.
   *
   * Derived classes must implement this method to bind to a network address and port,
   * making the node discoverable and able to accept connections from other peers.
   *
   * @param addr The address to bind to (e.g., "0.0.0.0" for all interfaces).
   * @param port The port number to listen on.
   * @return `true` if listening started successfully, `false` otherwise.
   */
  virtual bool listen(const std::string& addr, int port)=0;

  /**
   * @brief Pure virtual function to connect to a remote peer.
   *
   * Derived classes must implement this method to establish an outgoing connection
   * to another node in the network.
   *
   * @param host The hostname or IP address of the peer to connect to.
   * @param port The port number of the peer.
   * @return `true` if the connection was successful, `false` otherwise.
   */
  virtual bool connect(const std::string& host, int port)=0;

  /**
   * @brief Pure virtual function to publish a message to connected peers.
   *
   * Derived classes must implement this method to send a message to one or more
   * connected peers, potentially based on a topic.
   *
   * @param topic The topic of the message, used for routing or categorization.
   * @param msg The message data as a `Bytes` object.
   * @return `true` if the message was successfully published, `false` otherwise.
   */
  virtual bool publish(const std::string& topic, const Bytes& msg)=0;

  /**
   * @brief Pure virtual function to send a message directly to a specific peer.
   *
   * @param peer_id The identifier of the peer to send the message to.
   * @param msg The message data as a `Bytes` object.
   * @return `true` if the message was successfully sent, `false` otherwise.
   */
  virtual bool send_direct(const std::string& peer_id, const Bytes& msg)=0;

  /**
   * @brief Pure virtual function to set a callback for handling incoming messages.
   *
   * Derived classes must implement this method to register a `MsgHandler` function
   * that will be invoked whenever a message is received from a peer.
   *
   * @param cb The `MsgHandler` function to be called when a message is received.
   */
  virtual void on_message(MsgHandler cb)=0;
};