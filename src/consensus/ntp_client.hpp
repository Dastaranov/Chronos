//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file ntp_client.hpp
 * @brief Defines the NtpClient class for querying Network Time Protocol (NTP) servers.
 *
 * This class provides functionality to send NTP requests to a specified server
 * and parse the response to extract a timestamp. It is designed to be used
 * by the ExternalTimeSourceManager to gather time measurements from various
 * public NTP servers.
 */

#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include <chrono>

namespace chrono_consensus {

// Forward declaration for TimeMeasurement struct
struct TimeMeasurement; 

/**
 * @class NtpClient
 * @brief Handles NTP queries to a single NTP server.
 */
class NtpClient {
public:
    /**
     * @brief Queries a given NTP server for the current time.
     *
     * This method sends an NTP request packet to the specified server and port,
     * waits for a response, and extracts the receive timestamp from the NTP response.
     *
     * @param server The hostname or IP address of the NTP server.
     * @param port The port number of the NTP server (default 123).
     * @return An `std::optional<uint64_t>` containing the NTP server's receive timestamp
     *         in milliseconds since epoch if successful, `std::nullopt` otherwise.
     */
    static std::optional<TimeMeasurement> query_ntp_server(const std::string& server, int port = 123);

private:
    // NTP packet format details (simplified for receive timestamp)
    static constexpr int NTP_PACKET_SIZE = 48; // NTP packet size in bytes
    static constexpr int NTP_TIMESTAMP_OFFSET = 40; // Offset to the Transmit Timestamp field

    // Helper function to convert NTP timestamp to milliseconds since epoch
    static uint64_t ntp_to_unix_ms(uint64_t ntp_timestamp);
};

} // namespace chrono_consensus
