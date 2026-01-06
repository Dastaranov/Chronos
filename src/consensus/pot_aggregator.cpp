//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file pot_aggregator.cpp
 * @brief This file implements the PoTAggregator class for aggregating timestamps in a Proof-of-Time (PoT) consensus mechanism.
 *
 * The implementation details of the PoTAggregator class are provided here, including the constructor
 * and methods for adding timestamps and calculating the consensus time. The core logic involves
 * statistical analysis to determine a robust time value from a collection of potentially noisy timestamps,
 * using median and Median Absolute Deviation (MAD) to filter outliers.
 *
 * Key functions implemented:
 * - `PoTAggregator::PoTAggregator`: Constructor to initialize the aggregator with MAD factor and minimum threshold.
 * - `PoTAggregator::add_timestamp`: Adds a new timestamp to the internal collection.
 * - `PoTAggregator::get_consensus_time`: Calculates the aggregated consensus time, handling outliers and edge cases.
 */

#include "consensus/pot_aggregator.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace chrono_consensus {

/**
 * @brief Constructs a PoTAggregator object.
 *
 * Initializes the PoTAggregator with a specified Median Absolute Deviation (MAD) factor and
 * a minimum threshold for timestamps. These parameters are crucial for the outlier detection
 * and filtering process when calculating the consensus time.
 *
 * @param mad_factor A double representing the MAD factor. This value scales the MAD to define
 *                   the acceptable range for timestamps around the median.
 * @param min_thr_ms A double representing the minimum threshold in milliseconds. Timestamps
 *                   below this threshold might be considered invalid or ignored, depending on
 *                   further implementation details.
 */
PoTAggregator::PoTAggregator(double mad_factor, double min_thr_ms)
    : mad_factor_(mad_factor), min_thr_ms_(min_thr_ms) {}

bool PoTAggregator::verify_measurement(const TimeMeasurement& tm) const {
    // 1. Basic sanity check
    if (tm.timestamp == 0) return false;
    if (tm.confidence < 0.0 || tm.confidence > 1.0) return false;

    // 2. Statistical check (if we have enough data)
    if (time_measurements_.size() >= 5) {
        // Calculate current median using nth_element (O(N))
        std::vector<uint64_t> timestamps;
        timestamps.reserve(time_measurements_.size());
        for (const auto& m : time_measurements_) timestamps.push_back(m.timestamp);
        
        size_t n = timestamps.size();
        size_t mid = n / 2;
        std::nth_element(timestamps.begin(), timestamps.begin() + mid, timestamps.end());
        uint64_t median = timestamps[mid];
        if (n % 2 == 0) {
            std::nth_element(timestamps.begin(), timestamps.begin() + mid - 1, timestamps.begin() + mid);
            median = (timestamps[mid - 1] + median) / 2;
        }

        // Calculate MAD using nth_element (O(N))
        std::vector<uint64_t> deviations;
        deviations.reserve(timestamps.size());
        for (const auto& m : time_measurements_) {
            deviations.push_back(std::abs(static_cast<int64_t>(m.timestamp) - static_cast<int64_t>(median)));
        }
        
        std::nth_element(deviations.begin(), deviations.begin() + mid, deviations.end());
        uint64_t mad = deviations[mid]; // MAD is typically the median of deviations

        // Check if new measurement is within 3 * MAD (or some factor)
        // If MAD is 0 (all timestamps identical), allow small deviation (e.g. 100ms)
        uint64_t allowed_deviation = std::max(mad * 3, static_cast<uint64_t>(100));
        if (std::abs(static_cast<int64_t>(tm.timestamp) - static_cast<int64_t>(median)) > allowed_deviation) {
            return false; // Outlier
        }
    }

    return true;
}

void PoTAggregator::add_timestamp(const TimeMeasurement& tm) {
    // Basic validation: ignore timestamps that are too old or have very low confidence
    if (tm.timestamp < min_thr_ms_) { // Assuming min_thr_ms_ can also serve as a minimum valid time
        // Log or handle invalid timestamp
        return;
    }
    if (tm.confidence < 0.1) { // Example threshold for confidence
        // Log or handle low confidence timestamp
        return;
    }

    if (!verify_measurement(tm)) {
        return;
    }

    // Further anti-spoofing checks can be added here:
    // - Check for sudden jumps (compare with last added timestamp or current consensus time)
    // - Source-specific validation (e.g., if from NTP, check stratum, jitter etc. - would require more metadata)

    time_measurements_.push_back(tm);
}

uint64_t PoTAggregator::get_consensus_time() const {
    if (time_measurements_.empty()) {
        throw std::runtime_error("No time measurements to aggregate.");
    }

    // 1. Extract timestamps for median calculation (O(N))
    std::vector<uint64_t> timestamps_only;
    timestamps_only.reserve(time_measurements_.size());
    for (const auto& tm : time_measurements_) {
        timestamps_only.push_back(tm.timestamp);
    }
    
    size_t size = timestamps_only.size();
    size_t mid = size / 2;
    std::nth_element(timestamps_only.begin(), timestamps_only.begin() + mid, timestamps_only.end());
    uint64_t median_timestamp = timestamps_only[mid];
    
    if (size % 2 == 0) {
        std::nth_element(timestamps_only.begin(), timestamps_only.begin() + mid - 1, timestamps_only.begin() + mid);
        median_timestamp = (timestamps_only[mid - 1] + median_timestamp) / 2;
    }

    // 2. Calculate Median Absolute Deviation (MAD) (O(N))
    std::vector<uint64_t> deviations;
    deviations.reserve(size);
    for (const auto& tm : time_measurements_) {
        deviations.push_back(std::abs(static_cast<int64_t>(tm.timestamp) - static_cast<int64_t>(median_timestamp)));
    }
    
    std::nth_element(deviations.begin(), deviations.begin() + mid, deviations.end());
    uint64_t mad = deviations[mid];

    // 3. Filter out outliers and prepare for weighted average
    std::vector<TimeMeasurement> filtered_measurements;
    filtered_measurements.reserve(size);
    double total_weighted_sum = 0.0;
    double total_confidence = 0.0;

    for (const auto& tm : time_measurements_) {
        if (std::abs(static_cast<int64_t>(tm.timestamp) - static_cast<int64_t>(median_timestamp)) <= mad_factor_ * mad) {
            filtered_measurements.push_back(tm);
            total_weighted_sum += static_cast<double>(tm.timestamp) * tm.confidence;
            total_confidence += tm.confidence;
        }
    }

    // Fallback if all are filtered out
    if (filtered_measurements.empty()) {
        return median_timestamp;
    }

    // 4. Calculate weighted average of filtered timestamps
    if (total_confidence > 0) {
        return static_cast<uint64_t>(std::round(total_weighted_sum / total_confidence));
    } else {
        // This case should ideally not happen if filtered_measurements is not empty and confidence > 0
        // But as a safeguard, return the median of filtered timestamps if weights sum to zero.
        // Or re-calculate median from filtered_measurements if their confidence sums to zero.
        std::vector<uint64_t> filtered_timestamps_only;
        filtered_timestamps_only.reserve(filtered_measurements.size());
        for(const auto& tm : filtered_measurements) {
            filtered_timestamps_only.push_back(tm.timestamp);
        }
        std::sort(filtered_timestamps_only.begin(), filtered_timestamps_only.end());
        size_t filtered_size = filtered_timestamps_only.size();
        if (filtered_size % 2 == 0) {
            return (filtered_timestamps_only[filtered_size / 2 - 1] + filtered_timestamps_only[filtered_size / 2]) / 2;
        } else {
            return filtered_timestamps_only[filtered_size / 2];
        }
    }
}

} // namespace chrono_consensus