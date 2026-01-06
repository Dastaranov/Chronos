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
                                                     MeasurementCallback callback,
                                                     std::unique_ptr<ITimeSyncBackend> backend)
    : ntp_servers_(ntp_servers),
      query_interval_ms_(query_interval_ms),
      measurement_callback_(callback),
      running_(false),
      time_backend_(std::move(backend)),
      last_high_tier_success_time_(std::chrono::system_clock::now()) {
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "ExternalTimeSourceManager initialized with {} NTP servers.", ntp_servers_.size());
    if (time_backend_) {
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Using time backend: {}", time_backend_->get_name());
    }
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

uint32_t ExternalTimeSourceManager::get_current_tier() const {
    return current_tier_.load();
}

void ExternalTimeSourceManager::set_tier_for_testing(uint32_t tier) {
    current_tier_.store(tier);
}

double ExternalTimeSourceManager::get_time_quality_score() const {
    uint32_t tier = current_tier_.load();
    double base_score = 0.0;

    switch (tier) {
        case 1: base_score = 100.0; break; // Quantum
        case 2: base_score = 90.0; break;  // Atomic
        case 3: base_score = 80.0; break;  // GPS
        case 4: base_score = 60.0; break;  // NTS
        case 5: base_score = 20.0; break;  // NTP/Local
        default: base_score = 0.0; break;
    }

    // Penalize for consecutive failures
    int failures = consecutive_failures_.load();
    double penalty = failures * 5.0; // 5 points per failure
    
    double final_score = base_score - penalty;
    return std::max(0.0, final_score);
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

            if (time_backend_) {
                auto sample_opt = time_backend_->query(selected_server);
                if (sample_opt) {
                    consecutive_failures_ = 0;
                    if (sample_opt->tier <= 2) {
                        last_high_tier_success_time_ = std::chrono::system_clock::now();
                    }
                    current_tier_ = sample_opt->tier;

                    TimeMeasurement tm;
                    tm.timestamp = sample_opt->timestamp_ms + sample_opt->offset.count();
                    tm.source = sample_opt->authenticated ? TimeSource::CHRONY_NTS : TimeSource::NTP_POOL; 
                    tm.confidence = sample_opt->authenticated ? 1.0 : 0.8;
                    tm.error_ms = sample_opt->rtt_ms / 2.0;
                    tm.tier = sample_opt->tier;
                    tm.signature = sample_opt->signature;
                    
                    measurement_callback_(tm);
                } else {
                    consecutive_failures_++;
                    current_tier_ = 5; // Downgrade to NTP tier on failure
                    LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Backend failed to query: {}. Consecutive failures: {}", selected_server, consecutive_failures_.load());
                    
                    // Fallback to Tier 5 (System Clock)
                    TimeMeasurement tm;
                    tm.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
                    tm.source = TimeSource::LOCAL_OS;
                    tm.confidence = 0.1;
                    tm.error_ms = 1000.0;
                    tm.tier = 5;
                    measurement_callback_(tm);

                    // Check for prolonged downtime (Slashing condition)
                    auto now = std::chrono::system_clock::now();
                    auto downtime_hours = std::chrono::duration_cast<std::chrono::hours>(now - last_high_tier_success_time_).count();
                    if (downtime_hours >= 24) {
                        LOG_ERROR(chrono_util::LogCategory::CONSENSUS, "CRITICAL: Time backend unavailable for {} hours. Node is non-compliant and risks slashing.", downtime_hours);
                    }
                }
            } else {
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
            }
        } else {
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, "No NTP servers configured for ExternalTimeSourceManager.");
        }

        // Wait for the next query interval or until signaled to stop
        std::this_thread::sleep_for(std::chrono::milliseconds(query_interval_ms_));
    }
}

} // namespace chrono_consensus
