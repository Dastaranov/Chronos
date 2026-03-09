//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file ntp_client.cpp
 * @brief Implements the NtpClient class for querying Network Time Protocol (NTP) servers.
 */

#include "ntp_client.hpp"
#include "pot_aggregator.hpp" // For TimeMeasurement struct
#include "util/log.hpp" // For logging
#include <iostream>
#include <array>
#include <cstring>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif

namespace chrono_consensus {

/**
 * @struct NtpPacket
 * @brief Represents a simplified NTP (Network Time Protocol) packet structure.
 *
 * This structure is used to send and receive NTP messages. Only essential fields
 * for obtaining the server's transmit timestamp are detailed, others are for packet
 * conformity. NTP timestamps are 64-bit unsigned fixed-point numbers, representing
 * seconds relative to 00:00 on 1 January 1900.
 */
struct NtpPacket {
    uint8_t li_vn_mode;        ///< @var li_vn_mode Leap Indicator (LI), Version Number (VN), and Mode. LI: 2 bits, VN: 3 bits, Mode: 3 bits.
    uint8_t stratum;           ///< @var stratum Stratum Level of the local clock. Indicates the server's accuracy.
    uint8_t poll;              ///< @var poll Poll interval (log2 seconds).
    uint8_t precision;         ///< @var precision Precision of the local clock (log2 seconds).
    uint32_t root_delay;       ///< @var root_delay Total round-trip delay to the primary reference source.
    uint32_t root_dispersion;  ///< @var root_dispersion Maximum error relative to the primary reference source.
    uint32_t ref_id;           ///< @var ref_id Reference clock identifier.
    uint32_t ref_ts_int;       ///< @var ref_ts_int Reference timestamp (integer part) - time when system clock was last set or corrected.
    uint32_t ref_ts_frac;      ///< @var ref_ts_frac Reference timestamp (fractional part).
    uint32_t orig_ts_int;      ///< @var orig_ts_int Originate timestamp (integer part) - time when the request departed the client.
    uint32_t orig_ts_frac;     ///< @var orig_ts_frac Originate timestamp (fractional part).
    uint32_t recv_ts_int;      ///< @var recv_ts_int Receive timestamp (integer part) - time when the request arrived at the server.
    uint32_t recv_ts_frac;     ///< @var recv_ts_frac Receive timestamp (fractional part).
    uint32_t trans_ts_int;     ///< @var trans_ts_int Transmit timestamp (integer part) - time when the reply departed the server.
    uint32_t trans_ts_frac;    ///< @var trans_ts_frac Transmit timestamp (fractional part).
};

// NTP timestamp conversion constant
static constexpr uint64_t NTP_TIMESTAMP_DELTA = 2208988800ULL; // 1 Jan 1970 to 1 Jan 1900 in seconds

/**
 * @brief Queries a given NTP server for the current time.
 *
 * This method sends an NTP request packet to the specified server and port,
 * waits for a response, and extracts the receive timestamp from the NTP response.
 *
 * @param server The hostname or IP address of the NTP server.
 * @param port The port number of the NTP server (default 123).
 * @return An `std::optional<TimeMeasurement>` containing the NTP server's receive timestamp
 *         in milliseconds since epoch if successful, `std::nullopt` otherwise.
 */
std::optional<TimeMeasurement> NtpClient::query_ntp_server(const std::string& server, int port) {
#ifdef _WIN32
    // Initialize Winsock for Windows platforms
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "NtpClient: WSAStartup failed.");
        return std::nullopt;
    }
#endif

    int sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd < 0) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Failed to create socket for NTP client.");
#ifdef _WIN32
        WSACleanup();
#endif
        return std::nullopt;
    }

    struct hostent* host_entry = gethostbyname(server.c_str());
    if (!host_entry) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Failed to resolve NTP server address: {}", server);
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return std::nullopt;
    }

    sockaddr_in serv_addr;
    std::memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    std::memcpy(&serv_addr.sin_addr.s_addr, host_entry->h_addr, host_entry->h_length);
    serv_addr.sin_port = htons(port);

    // Initialize NTP packet to all zeros
    NtpPacket packet;
    std::memset(&packet, 0, sizeof(packet));

    // Set NTP packet header fields:
    // LI (Leap Indicator): 0 (no warning)
    // VN (Version Number): 3 (NTP Version 3)
    // Mode: 3 (Client mode)
    // Combined: (0 << 6) | (3 << 3) | 3 = 0x1B
    packet.li_vn_mode = 0x1B;

    // Send NTP request
    if (sendto(sockfd, (char*)&packet, sizeof(packet), 0, (sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "Failed to send NTP request to {}.", server);
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return std::nullopt;
    }

    // Set a timeout for receiving the response
    timeval timeout;
    timeout.tv_sec = 1; // 1 second timeout
    timeout.tv_usec = 0;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    // Receive NTP response
    socklen_t serv_addr_len = sizeof(serv_addr);
    if (recvfrom(sockfd, (char*)&packet, sizeof(packet), 0, (sockaddr*)&serv_addr, &serv_addr_len) < 0) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Timeout or error receiving NTP response from {}.", server);
#ifdef _WIN32
        closesocket(sockfd);
        WSACleanup();
#else
        close(sockfd);
#endif
        return std::nullopt;
    }

#ifdef _WIN32
    closesocket(sockfd);
    WSACleanup(); // Clean up Winsock
#else
    close(sockfd); // Close socket for Unix-like systems
#endif

    // Convert NTP receive timestamp from network byte order to host byte order
    // NTP timestamps are 64-bit fixed-point numbers.
    // The Unix epoch starts 70 years after the NTP epoch (1 Jan 1900).
    uint64_t ntp_recv_ts = (static_cast<uint64_t>(ntohl(packet.recv_ts_int)) << 32) | ntohl(packet.recv_ts_frac);
    
    // Convert NTP timestamp to Unix milliseconds
    uint64_t unix_ms = (ntp_recv_ts / (1ULL << 32)) * 1000 - (NTP_TIMESTAMP_DELTA * 1000);

    // Create a TimeMeasurement object
    TimeMeasurement tm;
    tm.timestamp = unix_ms;
    tm.source = TimeSource::NTP_POOL; // TODO: Determine specific NTP source or pass as param
    tm.confidence = 0.8; // TODO: Calculate confidence based on network conditions/server quality
    tm.error_ms = 50.0; // TODO: Estimate actual error

    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "NTP client received time from {}: {}", server, tm.timestamp);
    return tm;
}

} // namespace chrono_consensus
