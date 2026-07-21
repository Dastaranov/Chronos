//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_pot.cpp
 * @brief This file contains unit tests for the Proof-of-Time (PoT) aggregator logic in the Chronos project.
 *
 * It focuses on verifying the correct calculation of consensus time by the `PoTAggregator`
 * class, including its ability to filter out outliers based on the Median Absolute Deviation (MAD) factor.
 *
 * Key tests include:
 * - `test_pot_logic()`: Simulates adding several timestamps to the aggregator,
 *   including an outlier, and then asserts that the calculated consensus time
 *   is accurate after outlier filtering.
 */

#include "consensus/pot_aggregator.hpp"
#include "test_framework.hpp"
#include <cassert>
#include <vector>

namespace {

/**
 * @brief Tests the Proof-of-Time (PoT) aggregation logic, including outlier filtering.
 *
 * This test case performs the following steps:
 * 1. Initializes a `PoTAggregator` with a specific MAD factor and minimum threshold.
 * 2. Adds a series of timestamps, including one clear outlier.
 * 3. Calls `get_consensus_time()` to trigger the aggregation and filtering process.
 * 4. Asserts that the calculated `consensus_time` matches the expected value after
 *    the outlier has been successfully filtered out. The comments detail the expected
 *    median, deviations, MAD, filter threshold, filtered timestamps, and final mean calculation.
 */
void test_pot_logic() {
    // Create a PoT aggregator with a MAD factor of 2.0 and a min threshold of 10ms
    chrono_consensus::PoTAggregator aggregator(2.0, 10.0); ///< @var aggregator Instance of the PoT aggregator for testing.

    // Add some timestamps, including an outlier (200)
    aggregator.add_timestamp(100);
    aggregator.add_timestamp(105);
    aggregator.add_timestamp(95);
    aggregator.add_timestamp(102);
    aggregator.add_timestamp(98);
    aggregator.add_timestamp(200); // Outlier

    // Get the consensus time, which should filter out the outlier
    uint64_t consensus_time = aggregator.get_consensus_time(); ///< @var consensus_time The calculated consensus time after aggregation and filtering.

    // Expected calculation breakdown:
    // The median of (95, 98, 100, 102, 105, 200) is (100+102)/2 = 101.
    // The deviations from the median (101) are:
    // |100-101|=1, |105-101|=4, |95-101|=6, |102-101|=1, |98-101|=3, |200-101|=99.
    // The sorted absolute deviations are: 1, 1, 3, 4, 6, 99.
    // The median of absolute deviations (MAD) is (3+4)/2 = 3.5.
    // The filter threshold is MAD factor * MAD = 2.0 * 3.5 = 7.
    // Timestamps within the filter: |ts - 101| <= 7.
    // These are: 100, 105, 95, 102, 98. (200 is filtered out as |200-101|=99 > 7)
    // The mean of the filtered timestamps is (100 + 105 + 95 + 102 + 98) / 5 = 500 / 5 = 100.
    ASSERT_EQ(consensus_time, 100, "Consensus time should be 100 after filtering outlier.");
}

/**
 * @brief Tests the PoT proof inequality validator for a passing case.
 */
void test_pot_timestamp_inequality_accepts_valid_timestamp() {
    // T_p <= T_v + 2*epsilon - delta_min
    const uint64_t t_v = 1000;
    const uint64_t epsilon = 10;
    const uint64_t delta_min = 5;
    const uint64_t rhs = t_v + (2 * epsilon) - delta_min; // 1015

    ASSERT_TRUE(
        chrono_consensus::PoTAggregator::validate_timestamp(rhs, t_v, epsilon, delta_min),
        "Timestamp equal to PoT bound should be accepted.");
}

/**
 * @brief Tests the PoT proof inequality validator for a failing case.
 */
void test_pot_timestamp_inequality_rejects_future_timestamp() {
    const uint64_t t_v = 1000;
    const uint64_t epsilon = 10;
    const uint64_t delta_min = 5;
    const uint64_t rhs = t_v + (2 * epsilon) - delta_min; // 1015

    ASSERT_FALSE(
        chrono_consensus::PoTAggregator::validate_timestamp(rhs + 1, t_v, epsilon, delta_min),
        "Timestamp above PoT bound should be rejected.");
}

/**
 * @brief Tests PoT validator behavior when RHS would become negative.
 */
void test_pot_timestamp_inequality_negative_rhs_rejects() {
    ASSERT_FALSE(
        chrono_consensus::PoTAggregator::validate_timestamp(
            0,
            1,
            0,
            10),
        "If T_v + 2*epsilon - delta_min is negative, validation must reject.");
}

/**
 * @struct Registrar
 * @brief A helper struct to automatically register test cases with the test framework.
 *
 * This struct's constructor is executed at static initialization time,
 * registering `test_pot_logic` with the `test_framework`.
 */
struct Registrar {
    Registrar() {
        test_framework::register_test("PoT Logic", test_pot_logic);
        test_framework::register_test("PoT Timestamp Inequality Accept", test_pot_timestamp_inequality_accepts_valid_timestamp);
        test_framework::register_test("PoT Timestamp Inequality Reject", test_pot_timestamp_inequality_rejects_future_timestamp);
        test_framework::register_test("PoT Timestamp Inequality Negative RHS", test_pot_timestamp_inequality_negative_rhs_rejects);
    }
};

static Registrar registrar; ///< @var registrar Static instance of Registrar to trigger test registration.

} // namespace