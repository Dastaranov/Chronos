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
#include "consensus/ITimeSyncBackend.hpp" // For ITimeSyncBackend
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
     * @param backend Optional backend for time synchronization (defaults to nullptr/legacy).
     */
    ExternalTimeSourceManager(const std::vector<std::string>& ntp_servers,
                              long query_interval_ms,
                              MeasurementCallback callback,
                              std::unique_ptr<ITimeSyncBackend> backend = nullptr);

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

    /**
     * @brief Gets the current time tier based on recent measurements.
     * @return The current TimeTier (1-5).
     */
    uint32_t get_current_tier() const;

    /**
     * @brief Sets the current time tier manually (FOR TESTING ONLY).
     * @param tier The tier to set.
     */
    void set_tier_for_testing(uint32_t tier);

    /**
     * @brief Calculates the Time Quality Score based on tier and stability.
     * @return A score between 0.0 and 100.0.
     */
    double get_time_quality_score() const;

private:
    std::vector<std::string> ntp_servers_;
    long query_interval_ms_;
    MeasurementCallback measurement_callback_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    std::unique_ptr<ITimeSyncBackend> time_backend_;
    std::atomic<int> consecutive_failures_{0};
    std::chrono::system_clock::time_point last_high_tier_success_time_;
    std::atomic<uint32_t> current_tier_{5}; // Default to NTP tier

    /**
     * @brief The main function executed by the worker thread.
     * This function periodically queries NTP servers and invokes the callback.
     */
    void worker_loop();
};

} // namespace chrono_consensus
