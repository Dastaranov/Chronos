//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file external_time_source_manager.cpp
 * @brief Implements the ExternalTimeSourceManager class for managing and querying diverse external time sources.
 */

#include "external_time_source_manager.hpp"
#include "util/log.hpp" // For logging
#include <random> // For randomizing NTP server selection

namespace chrono_consensus {

ExternalTimeSourceManager::ExternalTimeSourceManager(const std::vector<std::string>& ntp_servers,
                                                     long query_interval_ms,
                                                     MeasurementCallback callback)
    : ntp_servers_(ntp_servers),
      query_interval_ms_(query_interval_ms),
      measurement_callback_(callback),
      running_(false) {
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "ExternalTimeSourceManager initialized with {} NTP servers.", ntp_servers_.size());
}

ExternalTimeSourceManager::~ExternalTimeSourceManager() {
    stop();
}

void ExternalTimeSourceManager::start() {
    if (!running_.load()) {
        running_.store(true);
        worker_thread_ = std::thread(&ExternalTimeSourceManager::worker_loop, this);
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "ExternalTimeSourceManager worker thread started.");
    }
}

void ExternalTimeSourceManager::stop() {
    if (running_.load()) {
        running_.store(false);
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "ExternalTimeSourceManager worker thread stopped.");
    }
}

void ExternalTimeSourceManager::worker_loop() {
    // Random number generator for selecting NTP servers
    std::random_device rd;
    std::mt19937 gen(rd());

    while (running_.load()) {
        if (!ntp_servers_.empty()) {
            // Randomly select an NTP server
            std::uniform_int_distribution<> distrib(0, ntp_servers_.size() - 1);
            std::string selected_server = ntp_servers_[distrib(gen)];

            LOG_DEBUG(chrono_util::LogCategory::CONSENSUS, "Querying NTP server: {}", selected_server);

            std::optional<TimeMeasurement> tm_opt = NtpClient::query_ntp_server(selected_server);

            if (tm_opt) {
                // Adjust TimeMeasurement properties as needed before passing to callback
                tm_opt->source = TimeSource::NTP_POOL; // Assign source based on actual server type or configuration
                tm_opt->confidence = 0.8; // Example: Default confidence for NTP
                tm_opt->error_ms = 50.0;  // Example: Default error for NTP

                measurement_callback_(*tm_opt);
            } else {
                LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Failed to get time from NTP server: {}", selected_server);
            }
        } else {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, "No NTP servers configured for ExternalTimeSourceManager.");
        }

        // Wait for the next query interval or until signaled to stop
        std::this_thread::sleep_for(std::chrono::milliseconds(query_interval_ms_));
    }
}

} // namespace chrono_consensus
