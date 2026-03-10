#pragma once
#include "util/log.hpp"
#include "util/bytes.hpp" // NEW: For Bytes type
using chrono_util::Bytes; // NEW: Bring Bytes into scope
#include <string>
#include <functional> // For std::function
#include <arpa/inet.h> // For inet_addr, sockaddr_in, etc.
#include <unistd.h>    // For close
#include <vector>      // For Bytes


#include "crypto/aes_crypto.hpp"
#include "crypto/kyber_crypto.hpp"
#include "crypto/blake3.hpp" // For key derivation
#include <random>

namespace chrono_p2p {

using MsgHandler = std::function<void(const std::string& topic, const Bytes& data, const std::string& sender_id)>;

/**
 * @class P2pClient
 * @brief Provides basic client-side P2P networking functionalities with Kyber encryption.
 */
class P2pClient {
public:
    P2pClient();
    ~P2pClient();

    bool connect_to_peer(const std::string& ip_address, int port);
    bool send_message(const Bytes& message);
    bool is_connected() const { return connected_; }
    void disconnect();
    bool receive_message();
    void set_message_handler(MsgHandler handler);

    // Reconnection settings
    void set_auto_reconnect(bool enable) { auto_reconnect_ = enable; }
    void set_reconnect_config(int delay_ms, int max_attempts) {
        reconnect_delay_ms_ = delay_ms;
        max_reconnect_attempts_ = max_attempts;
    }

private:
    bool send_n_bytes(const void* buffer, size_t n_bytes);
    bool recv_n_bytes(void* buffer, size_t n_bytes);
    
    // Handshake methods
    bool perform_handshake();

    // Reconnection logic
    bool handle_disconnect();
    
    // Encryption helpers
    Bytes session_key_;
    bool secure_mode_ = false;
    
    int sock_;
    bool connected_;
    MsgHandler message_handler_;
    std::string peer_address_;

    // Connection details for reconnection
    std::string server_ip_;
    int server_port_ = 0;
    bool auto_reconnect_ = true;
    int reconnect_delay_ms_ = 1000;
    int max_reconnect_attempts_ = 5;
};

} // namespace chrono_p2p
