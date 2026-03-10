//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file p2p_client.cpp
 * @brief This file implements the P2pClient class, providing basic client-side P2P networking functionalities with Kyber encryption.
 */

#include "p2p/p2p_client.hpp"

#include "crypto/aes_crypto.hpp"
#include "crypto/kyber_crypto.hpp"
#include "crypto/blake3.hpp"
#include <cstring> // For memset
#include <fcntl.h> // For fcntl
#include <vector> // For Bytes
#include <thread> // For std::this_thread::sleep_for
#include <chrono> // For std::chrono::milliseconds
#include <random>
#include <algorithm>
#include <functional> // For std::ref

namespace chrono_p2p {

P2pClient::P2pClient() : sock_(-1), connected_(false), secure_mode_(false) {}

P2pClient::~P2pClient() {
    disconnect();
}

bool P2pClient::connect_to_peer(const std::string& ip_address, int port) {
    if (connected_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Already connected to a peer. Disconnect first.");
        return false;
    }

    // Save connection details for reconnection
    server_ip_ = ip_address;
    server_port_ = port;

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
    peer_address_ = ip_address + ":" + std::to_string(port);
    LOG_INFO(chrono_util::LogCategory::P2P, "Connected to peer {}:{}", ip_address, port);

    // Perform Kyber Handshake immediately after connection
    if (!perform_handshake()) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Kyber handshake failed with {}. Disconnecting.", peer_address_);
        disconnect();
        return false;
    }

    return true;
}

bool P2pClient::perform_handshake() {
    // 1. Initialize Kyber
    if (!chrono_crypto::KyberCrypto::init()) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to initialize KyberCrypto");
        return false;
    }

    // 2. Receive Server's Public Key
    size_t pk_size = chrono_crypto::KyberCrypto::get_public_key_size();
    if (pk_size == 0) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Invalid Kyber public key size");
        return false;
    }

    chrono_util::Bytes server_pk(pk_size);
    if (!recv_n_bytes(server_pk.data(), pk_size)) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to receive Kyber public key from server");
        return false;
    }

    // 3. Encapsulate Shared Secret
    auto enc_result = chrono_crypto::KyberCrypto::encapsulate(server_pk);
    if (!enc_result) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Kyber encapsulation failed");
        return false;
    }

    // 4. Send Ciphertext to Server
    if (!send_n_bytes(enc_result->ciphertext.data(), enc_result->ciphertext.size())) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send Kyber ciphertext to server");
        return false;
    }

    // 5. Derive Session Key (SHA256 of Shared Secret)
    // Use BLAKE3 as KDF since we have it handy and it's fast/secure
    session_key_ = chrono_crypto::blake3(enc_result->shared_secret);
    
    secure_mode_ = true;
    LOG_INFO(chrono_util::LogCategory::P2P, "Kyber handshake successful. Secure channel established with {}", peer_address_);
    
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

    if (secure_mode_) {
        // Generate random IV
        chrono_util::Bytes iv(chrono_crypto::AESCrypto::IV_SIZE);
        // Simple random IV generation (in production use CSPRNG)
        std::random_device rd;
        std::generate(iv.begin(), iv.end(), std::ref(rd));

        // Encrypt payload
        auto encrypted = chrono_crypto::AESCrypto::encrypt(session_key_, iv, message);
        if (!encrypted) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to encrypt message");
            return false;
        }

        // Frame: [Length (4)] [IV (12)] [Ciphertext+Tag (N+16)]
        // Note: encrypted contains Ciphertext + Tag
        uint32_t total_len = iv.size() + encrypted->size();
        
        if (!send_n_bytes(&total_len, sizeof(total_len))) return false;
        if (!send_n_bytes(iv.data(), iv.size())) return false;
        if (!send_n_bytes(encrypted->data(), encrypted->size())) return false;

    } else {
        // Plaintext fallback (should not happen if handshake enforced)
        uint32_t message_size = message.size();
        if (!send_n_bytes(&message_size, sizeof(message_size))) return false;
        if (!send_n_bytes(message.data(), message_size)) return false;
    }
    return true;
}

bool P2pClient::handle_disconnect() {
    // Close existing socket
    if (sock_ != -1) {
        close(sock_);
        sock_ = -1;
    }
    connected_ = false;
    secure_mode_ = false;

    if (!auto_reconnect_) {
        return false;
    }

    LOG_INFO(chrono_util::LogCategory::P2P, "Connection lost. Attempting to reconnect to {}:{}...", server_ip_, server_port_);

    int current_delay = reconnect_delay_ms_;
    for (int attempt = 1; attempt <= max_reconnect_attempts_; ++attempt) {
        LOG_INFO(chrono_util::LogCategory::P2P, "Reconnect attempt {}/{}", attempt, max_reconnect_attempts_);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(current_delay));
        
        // Try to connect again
        if (connect_to_peer(server_ip_, server_port_)) {
            LOG_INFO(chrono_util::LogCategory::P2P, "Reconnected successfully to {}:{}", server_ip_, server_port_);
            return true;
        }
        
        current_delay *= 2; // Exponential backoff
    }

    LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to reconnect after {} attempts.", max_reconnect_attempts_);
    return false;
}

bool P2pClient::receive_message() {
    if (!connected_) {
        return false;
    }

    uint32_t frame_len;
    if (!recv_n_bytes(&frame_len, sizeof(frame_len))) {
        if (handle_disconnect()) {
            return true; // Reconnected, keep loop alive
        }
        connected_ = false;
        return false;
    }

    if (secure_mode_) {
        // Frame: [IV (12)] [Ciphertext+Tag (N+16)]
        // frame_len includes IV size
        if (frame_len < chrono_crypto::AESCrypto::IV_SIZE + chrono_crypto::AESCrypto::TAG_SIZE) {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Received frame too small for secure message");
            connected_ = false;
            return false;
        }

        chrono_util::Bytes iv(chrono_crypto::AESCrypto::IV_SIZE);
        if (!recv_n_bytes(iv.data(), iv.size())) {
            if (handle_disconnect()) return true;
            connected_ = false;
            return false;
        }

        size_t encrypted_len = frame_len - iv.size();
        chrono_util::Bytes encrypted(encrypted_len);
        if (!recv_n_bytes(encrypted.data(), encrypted_len)) {
            if (handle_disconnect()) return true;
            connected_ = false;
            return false;
        }

        // Split Tag from Ciphertext (Tag is at end)
        size_t tag_size = chrono_crypto::AESCrypto::TAG_SIZE;
        size_t ct_size = encrypted_len - tag_size;
        
        chrono_util::Bytes ciphertext(encrypted.begin(), encrypted.begin() + ct_size);
        chrono_util::Bytes tag(encrypted.begin() + ct_size, encrypted.end());

        auto decrypted = chrono_crypto::AESCrypto::decrypt(session_key_, iv, ciphertext, tag);
        if (!decrypted) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to decrypt message from {}", peer_address_);
            connected_ = false;
            return false;
        }

        if (message_handler_) {
            message_handler_("p2p", *decrypted, peer_address_);
        }

    } else {
        // Plaintext fallback
        Bytes received_data(frame_len);
        if (!recv_n_bytes(received_data.data(), frame_len)) {
            if (handle_disconnect()) return true;
            connected_ = false;
            return false;
        }

        if (message_handler_) {
            message_handler_("p2p", received_data, peer_address_);
        }
    }
    return connected_;
}

void P2pClient::disconnect() {
    if (connected_) {
        close(sock_);
        sock_ = -1;
        connected_ = false;
        secure_mode_ = false;
        LOG_INFO(chrono_util::LogCategory::P2P, "Disconnected from peer.");
    }
}

void P2pClient::set_message_handler(MsgHandler handler) {
    message_handler_ = std::move(handler);
}

} // namespace chrono_p2p
