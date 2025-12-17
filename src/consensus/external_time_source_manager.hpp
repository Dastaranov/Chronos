//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file external_time_source_manager.hpp
 * @brief Defines the ExternalTimeSourceManager class for managing and querying diverse external time sources.
 *
 * This class orchestrates the retrieval of time measurements from various sources,
 * such as NTP servers, and provides them to the PoTAggregator. It manages a list
 * of configured time sources, handles periodic querying, and encapsulates the logic
 * for creating TimeMeasurement objects with appropriate metadata.
 */

#pragma once

#include "pot_aggregator.hpp" // For TimeMeasurement struct and TimeSource enum
#include "ntp_client.hpp"     // For NtpClient
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <atomic>
#include <thread>
#include <functional>

namespace chrono_consensus {

/**
 * @class ExternalTimeSourceManager
 * @brief Manages querying of external time sources and provides measurements to the PoTAggregator.
 *
 * This class is responsible for periodically querying configured NTP servers and potentially
 * other external time sources. It encapsulates the logic for obtaining timestamps,
 * assigning confidence scores, and creating TimeMeasurement objects for the PoTAggregator.
 */
class ExternalTimeSourceManager {
public:
    // Define a callback type that the manager will use to pass TimeMeasurement objects
    // to the PoTAggregator.
    using MeasurementCallback = std::function<void(const TimeMeasurement&)>;

    /**
     * @brief Constructs an ExternalTimeSourceManager object.
     *
     * @param ntp_servers A vector of NTP server hostnames or IP addresses to query.
     * @param query_interval_ms The interval in milliseconds at which to query external sources.
     * @param callback A function to call for each new TimeMeasurement obtained.
     */
    ExternalTimeSourceManager(const std::vector<std::string>& ntp_servers,
                              long query_interval_ms,
                              MeasurementCallback callback);

    /**
     * @brief Destructor for ExternalTimeSourceManager.
     * Stops any background threads.
     */
    ~ExternalTimeSourceManager();

    /**
     * @brief Starts the periodic querying of external time sources.
     * This method will launch a background thread to perform queries.
     */
    void start();

    /**
     * @brief Stops the periodic querying of external time sources.
     * This method signals the background thread to terminate.
     */
    void stop();

private:
    std::vector<std::string> ntp_servers_;
    long query_interval_ms_;
    MeasurementCallback measurement_callback_;
    std::atomic<bool> running_;
    std::thread worker_thread_;

    /**
     * @brief The main function executed by the worker thread.
     * This function periodically queries NTP servers and invokes the callback.
     */
    void worker_loop();
};

} // namespace chrono_consensus
