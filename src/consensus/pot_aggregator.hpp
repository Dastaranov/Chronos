//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file pot_aggregator.hpp
 * @brief This file defines the PoTAggregator class for aggregating timestamps in a Proof-of-Time (PoT) consensus mechanism.
 *
 * The PoTAggregator class is designed to collect and process timestamps from various sources
 * to determine a robust "consensus time." It uses statistical methods, potentially involving
 * a Median Absolute Deviation (MAD) factor and a minimum threshold, to filter out outliers
 * and arrive at a reliable time value for the network.
 *
 * Key functionalities include:
 * - `PoTAggregator(double mad_factor, double min_thr_ms)`: Constructor to initialize the aggregator with specific parameters.
 * - `add_timestamp(uint64_t timestamp)`: Adds a new timestamp to the collection for aggregation.
 * - `get_consensus_time() const`: Calculates and returns the current consensus time based on the collected timestamps.
 */

#pragma once

#include <vector>
#include <cstdint>

namespace chrono_consensus {

enum class TimeSource {
    UNKNOWN = 0,    ///< Default or unspecified time source.
    NTP_POOL,       ///< Timestamp obtained from the NTP Pool Project.
    GOOGLE_NTP,     ///< Timestamp obtained from Google Public NTP.
    CLOUDFLARE_NTP, ///< Timestamp obtained from Cloudflare NTP.
    NIST_NTP,       ///< Timestamp obtained from NIST Internet Time Service.
    TIMENL_NTP,     ///< Timestamp obtained from TimeNL (SIDN Labs).
    LOCAL_OS,       ///< Timestamp from the local operating system clock.
    PTP,            ///< Precision Time Protocol source.
    GPS,            ///< Global Positioning System time source.
    NETWORK_API,    ///< Timestamp from a general network time API (e.g., TimeAPI.io)
    CHRONY_NTS,     ///< Authenticated time from Chrony (NTS)
    ATOMIC_CLOCK    ///< Direct connection to an atomic clock
};

/**
 * @struct TimeMeasurement
 * @brief Encapsulates a timestamp along with its source, confidence, and potential error margin.
 *
 * This structure allows the PoTAggregator to perform more sophisticated validation and
 * weighted aggregation based on the quality and origin of each time measurement.
 */
struct TimeMeasurement {
    uint64_t timestamp;  ///< The actual timestamp value.
    TimeSource source;   ///< The source from which the timestamp was obtained.
    double confidence;   ///< A confidence score (0.0 to 1.0) indicating the trustworthiness of this measurement.
    double error_ms;     ///< Estimated error margin in milliseconds.
    uint8_t tier;        ///< Security tier (1=Atomic, 2=NTS, 3=NTP, 4=Local)
    std::vector<uint8_t> signature; ///< Digital signature for Tier 1/2

    // Default constructor
    TimeMeasurement(uint64_t ts = 0, TimeSource src = TimeSource::UNKNOWN, double conf = 0.5, double err = 0.0, uint8_t t = 4)
        : timestamp(ts), source(src), confidence(conf), error_ms(err), tier(t) {}
};

/**
 * @class PoTAggregator
 * @brief Aggregates timestamps from various sources to determine a robust Proof-of-Time (PoT) consensus time.
 *
 * This class collects timestamps and applies a statistical aggregation method to derive
 * a reliable consensus time. It is configured with parameters to handle potential outliers
 * and ensure the quality of the aggregated time.
 */
class PoTAggregator {
public:
    /**
     * @brief Constructs a PoTAggregator object.
     *
     * Initializes the aggregator with parameters that influence how timestamps are processed
     * and how the consensus time is calculated.
     *
     * @param mad_factor A double representing the Median Absolute Deviation (MAD) factor. This is used
     *                   to identify and potentially filter out outlier timestamps. A higher factor
     *                   allows for more deviation.
     * @param min_thr_ms A double representing the minimum threshold in milliseconds. This could be
     *                   used to ensure a minimum time difference or to filter out timestamps below a certain value.
     */
    PoTAggregator(double mad_factor, double min_thr_ms);

    /**
     * @brief Adds a new time measurement to the aggregator's collection.
     *
     * This method is used to feed individual timestamps into the aggregator. These timestamps
     * will be used in the calculation of the consensus time.
     *
     * @param tm The TimeMeasurement object to add.
     */
    void add_timestamp(const TimeMeasurement& tm);

    /**
     * @brief Calculates and returns the current consensus time.
     *
     * This method processes all collected timestamps, applies the configured MAD factor and
     * minimum threshold, and returns a single, aggregated consensus time. The exact aggregation
     * logic (e.g., median, trimmed mean) is implemented internally.
     *
     * @return The calculated consensus time as a 64-bit unsigned integer.
     */
    uint64_t get_consensus_time() const;

    /**
     * @brief Verifies if a measurement is statistically plausible.
     * @param tm The measurement to verify.
     * @return True if plausible, false if likely spoofed.
     */
    bool verify_measurement(const TimeMeasurement& tm) const;

    /**
     * @brief Validates proposer timestamp using the PoT proof inequality.
     *
     * A proposed block timestamp T_p is considered valid if:
     * T_p <= T_v + 2*epsilon - delta_min
     *
     * @param proposer_timestamp_ms Proposed block timestamp (T_p) in milliseconds.
     * @param local_consensus_time_ms Local validator consensus time (T_v) in milliseconds.
     * @param epsilon_ms Maximum clock uncertainty epsilon in milliseconds.
     * @param delta_min_ms Minimum network delay delta_min in milliseconds.
     * @return True if the proposer timestamp satisfies the inequality, false otherwise.
     */
    static bool validate_timestamp(uint64_t proposer_timestamp_ms,
                                   uint64_t local_consensus_time_ms,
                                   uint64_t epsilon_ms,
                                   uint64_t delta_min_ms);

private:
    double mad_factor_; ///< @var mad_factor_ The Median Absolute Deviation factor used for outlier detection.
    double min_thr_ms_; ///< @var min_thr_ms_ The minimum threshold in milliseconds.
    std::vector<TimeMeasurement> time_measurements_; ///< @var time_measurements_ A collection of raw timestamps added to the aggregator.
};

} // namespace chrono_consensus