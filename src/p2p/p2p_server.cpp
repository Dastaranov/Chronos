//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file p2p_server.cpp
 * @brief This file implements the P2pServer class, which provides basic server-side P2P networking functionalities with Kyber encryption.
 */

#include "p2p/p2p_server.hpp"
#include "crypto/kyber_crypto.hpp"
#include "crypto/aes_crypto.hpp"
#include "crypto/blake3.hpp"
#include <arpa/inet.h> // For inet_ntoa
#include <cstring> // For memset
#include <vector>

namespace chrono_p2p {

P2pServer::P2pServer(int port, MessageHandler handler, chrono_node::NodeStatus& status)
    : port_(port), server_fd_(-1), message_handler_(std::move(handler)), running_(false), status_(status) {}

P2pServer::~P2pServer() {
    stop();
}

bool P2pServer::start() {
    if (running_) {
        LOG_WARN(chrono_util::LogCategory::P2P, "P2P server already running.");
        return true;
    }

    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

    if ((server_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Socket creation failed on port {}: {} (errno={})", port_, strerror(errno), errno);
        return false;
    }

    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: setsockopt failed on port {}: {} (errno={})", port_, strerror(errno), errno);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Bind failed on port {}", port_);
        close(server_fd_);
        server_fd_ = -1;
        return false;
    }
    if (listen(server_fd_, 10) < 0) {
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
            if (running_) {
                LOG_ERROR(chrono_util::LogCategory::P2P, "P2pServer: Accept failed on port {}: {} (errno={})", port_, strerror(err), err);
            }
            continue;
        }
        LOG_INFO(chrono_util::LogCategory::P2P, "P2pServer: Accepted new connection on port {} from {}:{}", port_, inet_ntoa(address.sin_addr), ntohs(address.sin_port));
        status_.connected_peers++;
        
        struct sockaddr_in client_addr_info;
        socklen_t client_addr_len = sizeof(client_addr_info);
        getpeername(client_socket, (struct sockaddr*)&client_addr_info, &client_addr_len);
        std::string client_ip = inet_ntoa(client_addr_info.sin_addr);
        int client_port = ntohs(client_addr_info.sin_port);
        std::string client_address = client_ip + ":" + std::to_string(client_port);

        // IP Limiting Check
        {
            std::lock_guard<std::mutex> lock(ip_counts_mutex_);
            if (ip_counts_[client_ip] >= MAX_PEERS_PER_IP) {
                LOG_WARN(chrono_util::LogCategory::P2P, "Rejecting connection from {}: IP limit reached ({}/{})", 
                         client_ip, ip_counts_[client_ip], MAX_PEERS_PER_IP);
                close(client_socket);
                continue;
            }
            ip_counts_[client_ip]++;
        }

        std::thread client_handler(&P2pServer::handle_client, this, client_socket, client_address, client_ip);
        client_handler.detach();
    }
    LOG_INFO(chrono_util::LogCategory::P2P, "P2pServer: run_server loop terminated for port {}", port_);
}

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
        if (received <= 0) {
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

bool P2pServer::perform_handshake(int client_socket, chrono_util::Bytes& session_key) {
    // 1. Initialize Kyber
    if (!chrono_crypto::KyberCrypto::init()) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to initialize KyberCrypto");
        return false;
    }

    // 2. Generate Ephemeral KeyPair
    auto kp = chrono_crypto::KyberCrypto::generate_keypair();
    if (!kp) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to generate Kyber keypair");
        return false;
    }

    // 3. Send Public Key to Client
    if (!send_n_bytes(client_socket, kp->public_key.data(), kp->public_key.size())) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to send Kyber public key to client");
        return false;
    }

    // 4. Receive Ciphertext from Client
    size_t ct_size = chrono_crypto::KyberCrypto::get_ciphertext_size();
    chrono_util::Bytes ciphertext(ct_size);
    if (!recv_n_bytes(client_socket, ciphertext.data(), ct_size)) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to receive Kyber ciphertext from client");
        return false;
    }

    // 5. Decapsulate Shared Secret
    auto shared_secret = chrono_crypto::KyberCrypto::decapsulate(ciphertext, kp->private_key);
    if (!shared_secret) {
        LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Kyber decapsulation failed");
        return false;
    }

    // 6. Derive Session Key (SHA256 of Shared Secret)
    session_key = chrono_crypto::blake3(*shared_secret);
    
    LOG_INFO(chrono_util::LogCategory::P2P, "Kyber handshake successful. Secure channel established.");
    return true;
}

void P2pServer::handle_client(int client_socket, const std::string& client_address, const std::string& client_ip) {
    chrono_util::Bytes session_key;
    
    // Perform Kyber Handshake
    if (!perform_handshake(client_socket, session_key)) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Kyber handshake failed with {}. Closing connection.", client_address);
        close(client_socket);
        status_.connected_peers--;
        
        // Decrement IP count
        {
            std::lock_guard<std::mutex> lock(ip_counts_mutex_);
            if (ip_counts_[client_ip] > 0) {
                ip_counts_[client_ip]--;
            }
        }
        return;
    }

    while (running_) {
        uint32_t frame_len;
        if (!recv_n_bytes(client_socket, &frame_len, sizeof(frame_len))) {
            break;
        }

        // Frame: [IV (12)] [Ciphertext+Tag (N+16)]
        if (frame_len < chrono_crypto::AESCrypto::IV_SIZE + chrono_crypto::AESCrypto::TAG_SIZE) {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Received frame too small for secure message from {}", client_address);
            break;
        }

        chrono_util::Bytes iv(chrono_crypto::AESCrypto::IV_SIZE);
        if (!recv_n_bytes(client_socket, iv.data(), iv.size())) {
            break;
        }

        size_t encrypted_len = frame_len - iv.size();
        chrono_util::Bytes encrypted(encrypted_len);
        if (!recv_n_bytes(client_socket, encrypted.data(), encrypted_len)) {
            break;
        }

        // Split Tag from Ciphertext
        size_t tag_size = chrono_crypto::AESCrypto::TAG_SIZE;
        size_t ct_size = encrypted_len - tag_size;
        
        chrono_util::Bytes ciphertext(encrypted.begin(), encrypted.begin() + ct_size);
        chrono_util::Bytes tag(encrypted.begin() + ct_size, encrypted.end());

        auto decrypted = chrono_crypto::AESCrypto::decrypt(session_key, iv, ciphertext, tag);
        if (!decrypted) {
            LOG_ERROR(chrono_util::LogCategory::CRYPTO, "Failed to decrypt message from {}", client_address);
            break;
        }

        // Convert decrypted Bytes to std::string for handler
        std::string received_message_data(decrypted->begin(), decrypted->end());
        message_handler_(client_address, received_message_data);
    }

    close(client_socket);
    status_.connected_peers--;
    
    // Decrement IP count
    {
        std::lock_guard<std::mutex> lock(ip_counts_mutex_);
        if (ip_counts_[client_ip] > 0) {
            ip_counts_[client_ip]--;
        }
    }
    
    LOG_INFO(chrono_util::LogCategory::P2P, "Client handler for {} terminated.", client_address);
}

} // namespace chrono_p2p
