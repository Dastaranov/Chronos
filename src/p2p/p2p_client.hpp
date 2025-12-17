#pragma once
#include "util/log.hpp"
#include "util/bytes.hpp" // NEW: For Bytes type
using chrono_util::Bytes; // NEW: Bring Bytes into scope
#include <string>
#include <functional> // For std::function
#include <arpa/inet.h> // For inet_addr, sockaddr_in, etc.
#include <unistd.h>    // For close
#include <vector>      // For Bytes

namespace chrono_p2p {

using MsgHandler = std::function<void(const std::string& topic, const Bytes& data, const std::string& sender_id)>;

/**
 * @class P2pClient
 * @brief Provides basic client-side P2P networking functionalities.
 *
 * This class encapsulates the logic for establishing and managing a client-side
 * connection to a peer in a peer-to-peer network. It provides methods for connecting to a peer,
 * sending messages, receiving messages, and disconnecting. This implementation uses standard
 * Berkeley sockets for network communication.
 */
class P2pClient {
public:
    P2pClient();
    ~P2pClient();

    bool connect_to_peer(const std::string& ip_address, int port);
    bool send_message(const Bytes& message);
    bool is_connected() const { return connected_; }
    void disconnect();

    /**
     * @brief Receives a binary message from the connected peer.
     *
     * This method reads length-prefixed binary data from the socket.
     * It's a blocking call that will wait for data. It handles peer disconnections and
     * receive errors by logging and returning false.
     *
     * @return `true` if the connection is still active and messages can be received, `false` otherwise (disconnected or error).
     */
    bool receive_message();

    /**
     * @brief Sets a callback function to handle incoming messages.
     *
     * This handler will be invoked whenever a message is received from the peer.
     *
     * @param handler A `MsgHandler` function object that takes the received message as `Bytes` and the sender's address.
     */
    void set_message_handler(MsgHandler handler);

private: // NEW: Helper methods for socket I/O
    // Helper to send exactly N bytes
    bool send_n_bytes(const void* buffer, size_t n_bytes);
    // Helper to receive exactly N bytes
    bool recv_n_bytes(void* buffer, size_t n_bytes);

private:
    int sock_; ///< @var sock_ Socket file descriptor.
    bool connected_; ///< @var connected_ Connection status.
    MsgHandler message_handler_; ///< @var message_handler_ Callback for received messages.
    std::string peer_address_; ///< @var peer_address_ Stores the "ip:port" address of the connected peer.
};

} // namespace chrono_p2p
