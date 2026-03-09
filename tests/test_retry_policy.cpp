/**
 * @file test_retry_policy.cpp
 * @brief Test suite for automatic retry mechanisms with exponential backoff.
 *
 * Tests validate:
 * - Exponential backoff calculation
 * - Backoff caps at maximum duration
 * - Jitter implementation
 * - Retry on retriable errors only
 * - Success on first attempt
 * - Exhaustion after max retries
 * - Integration with Result<T> is_retriable()
 */

#include "test_framework.hpp"
#include "util/retry_policy.hpp"
#include <chrono>

using namespace chrono_error;

TEST_CASE(BackoffCalculation_Initial, "Initial backoff is initial_backoff_ms") {
    RetryPolicy policy;
    policy.initial_backoff_ms = 100;
    policy.backoff_multiplier = 2.0;
    
    int backoff = calculate_backoff_ms(0, policy);
    ASSERT_EQ(backoff, 100, "Attempt 0 should be initial_backoff_ms");
}

TEST_CASE(BackoffCalculation_Exponential, "Backoff doubles exponentially") {
    RetryPolicy policy;
    policy.initial_backoff_ms = 100;
    policy.backoff_multiplier = 2.0;
    policy.max_backoff_ms = 10000;
    
    int backoff0 = calculate_backoff_ms(0, policy);
    int backoff1 = calculate_backoff_ms(1, policy);
    int backoff2 = calculate_backoff_ms(2, policy);
    int backoff3 = calculate_backoff_ms(3, policy);
    
    ASSERT_EQ(backoff0, 100, "Attempt 0: 100ms");
    ASSERT_EQ(backoff1, 200, "Attempt 1: 200ms");
    ASSERT_EQ(backoff2, 400, "Attempt 2: 400ms");
    ASSERT_EQ(backoff3, 800, "Attempt 3: 800ms");
}

TEST_CASE(BackoffCalculation_Capped, "Backoff is capped at max_backoff_ms") {
    RetryPolicy policy;
    policy.initial_backoff_ms = 100;
    policy.backoff_multiplier = 2.0;
    policy.max_backoff_ms = 500;
    
    int backoff3 = calculate_backoff_ms(3, policy);  // Would be 800
    int backoff5 = calculate_backoff_ms(5, policy);  // Would be 3200
    
    ASSERT_EQ(backoff3, 500, "Backoff capped at max_backoff_ms");
    ASSERT_EQ(backoff5, 500, "Backoff stays capped");
}

TEST_CASE(BackoffCalculation_CustomMultiplier, "Custom backoff multiplier works") {
    RetryPolicy policy;
    policy.initial_backoff_ms = 50;
    policy.backoff_multiplier = 3.0;
    policy.max_backoff_ms = 10000;
    
    int backoff0 = calculate_backoff_ms(0, policy);
    int backoff1 = calculate_backoff_ms(1, policy);
    int backoff2 = calculate_backoff_ms(2, policy);
    
    ASSERT_EQ(backoff0, 50, "Attempt 0: 50ms");
    ASSERT_EQ(backoff1, 150, "Attempt 1: 150ms (50 * 3)");
    ASSERT_EQ(backoff2, 450, "Attempt 2: 450ms (50 * 9)");
}

TEST_CASE(RetryPolicy_Validation_MaxRetries, "Validation rejects max_retries < 1") {
    RetryPolicy policy;
    policy.max_retries = 0;
    
    bool threw = false;
    try {
        policy.validate();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    
    ASSERT_TRUE(threw, "Should throw on max_retries < 1");
}

TEST_CASE(RetryPolicy_Validation_BackoffRange, "Validation rejects max_backoff < initial") {
    RetryPolicy policy;
    policy.initial_backoff_ms = 1000;
    policy.max_backoff_ms = 500;
    
    bool threw = false;
    try {
        policy.validate();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    
    ASSERT_TRUE(threw, "Should throw on max_backoff < initial_backoff");
}

TEST_CASE(RetryWithBackoff_Success_Immediate, "Succeeds immediately without retry") {
    int call_count = 0;
    
    Result<int> result = retry_with_backoff<int>(
        [&call_count]() {
            call_count++;
            return Result<int>(42);
        },
        3,  // max_retries
        10  // initial_backoff_ms (short for tests)
    );
    
    ASSERT_TRUE(result.is_success(), "Should be successful");
    ASSERT_EQ(result.value_or(0), 42, "Should return correct value");
    ASSERT_EQ(call_count, 1, "Should only call operation once");
}

TEST_CASE(RetryWithBackoff_Failure_NonRetriable, "Fails immediately on non-retriable error") {
    int call_count = 0;
    
    Result<int> result = retry_with_backoff<int>(
        [&call_count]() {
            call_count++;
            return Result<int>(StorageErrorCode::NotFound, "Not found");  // Non-retriable
        },
        3,
        10
    );
    
    ASSERT_FALSE(result.is_success(), "Should be failure");
    ASSERT_EQ(call_count, 1, "Should only call operation once (no retry)");
}

TEST_CASE(RetryWithBackoff_Failure_Retriable_Retries, "Retries on retriable error") {
    int call_count = 0;
    
    Result<int> result = retry_with_backoff<int>(
        [&call_count]() {
            call_count++;
            if (call_count < 3) {
                return Result<int>(StorageErrorCode::IOError, "Timeout");  // Retriable
            }
            return Result<int>(99);  // Success on 3rd try
        },
        3,  // max_retries
        10  // short backoff for tests
    );
    
    ASSERT_TRUE(result.is_success(), "Should eventually succeed");
    ASSERT_EQ(result.value_or(0), 99, "Should return success value");
    ASSERT_EQ(call_count, 3, "Should call operation 3 times");
}

TEST_CASE(RetryWithBackoff_Failure_ExhaustedRetries, "Gives up after max_retries") {
    int call_count = 0;
    
    Result<int> result = retry_with_backoff<int>(
        [&call_count]() {
            call_count++;
            return Result<int>(StorageErrorCode::IOError, "Always fails");  // Always retriable failure
        },
        3,  // max_retries
        10
    );
    
    ASSERT_FALSE(result.is_success(), "Should fail");
    ASSERT_EQ(call_count, 3, "Should call operation max_retries times");
}

TEST_CASE(RetryWithBackoff_Timing, "Applies backoff delay between retries") {
    int call_count = 0;
    auto start_time = std::chrono::steady_clock::now();
    
    Result<int> result = retry_with_backoff<int>(
        [&call_count]() {
            call_count++;
            if (call_count < 3) {
                return Result<int>(NetworkErrorCode::ConnectionTimeout, "Timeout");  // Retriable
            }
            return Result<int>(1);
        },
        3,
        50  // 50ms initial backoff
    );
    
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time
    ).count();
    
    ASSERT_TRUE(result.is_success(), "Should succeed after retries");
    ASSERT_EQ(call_count, 3, "Should retry twice");
    // With 50ms + 100ms backoff, we expect at least 150ms total
    // Allow some tolerance for timing variance
    ASSERT_TRUE(elapsed_ms >= 120, "Should apply backoff delays");
}

TEST_CASE(RetryOnError_Success, "Succeeds immediately on first try") {
    int call_count = 0;
    RetryPolicy policy;
    policy.max_retries = 3;
    policy.initial_backoff_ms = 10;
    
    Result<int> result = retry_on_error<int>(
        [&call_count]() {
            call_count++;
            return Result<int>(42);
        },
        policy
    );
    
    ASSERT_TRUE(result.is_success(), "Should succeed");
    ASSERT_EQ(call_count, 1, "Should only call once");
}

TEST_CASE(RetryOnError_TransientFailure, "Retries on transient network errors") {
    int call_count = 0;
    RetryPolicy policy;
    policy.max_retries = 3;
    policy.initial_backoff_ms = 10;
    
    Result<int> result = retry_on_error<int>(
        [&call_count]() {
            call_count++;
            if (call_count < 2) {
                return Result<int>(NetworkErrorCode::ConnectionTimeout, "Timeout");  // Retriable
            }
            return Result<int>(123);
        },
        policy
    );
    
    ASSERT_TRUE(result.is_success(), "Should succeed after retry");
    ASSERT_EQ(call_count, 2, "Should call twice");
}

TEST_CASE(RetryOnError_NoRetryNonRetriable, "Does not retry non-retriable errors") {
    int call_count = 0;
    RetryPolicy policy;
    policy.max_retries = 3;
    policy.initial_backoff_ms = 10;
    
    Result<int> result = retry_on_error<int>(
        [&call_count]() {
            call_count++;
            return Result<int>(StorageErrorCode::NotFound, "Not found");  // Non-retriable
        },
        policy
    );
    
    ASSERT_FALSE(result.is_success(), "Should fail");
    ASSERT_EQ(call_count, 1, "Should not retry non-retriable error");
}

TEST_CASE(RetryOnError_ConsensusTimeout, "Retries on consensus round timeout") {
    int call_count = 0;
    RetryPolicy policy;
    policy.max_retries = 2;
    policy.initial_backoff_ms = 10;
    
    Result<bool> result = retry_on_error<bool>(
        [&call_count]() {
            call_count++;
            if (call_count == 1) {
                return Result<bool>(ConsensusErrorCode::RoundTimeout, "Round timed out");  // Retriable
            }
            return Result<bool>(true);
        },
        policy
    );
    
    ASSERT_TRUE(result.is_success(), "Should succeed after retry");
    ASSERT_EQ(call_count, 2, "Should retry consensus timeout");
}

TEST_CASE(RetryPolicy_Zero_Backoff, "Handles zero backoff duration") {
    RetryPolicy policy;
    policy.initial_backoff_ms = 0;
    policy.max_retries = 2;
    
    int call_count = 0;
    auto start = std::chrono::steady_clock::now();
    
    Result<int> result = retry_on_error<int>(
        [&call_count]() {
            call_count++;
            if (call_count < 2) {
                return Result<int>(NetworkErrorCode::MessageSendFailed, "Failed");
            }
            return Result<int>(1);
        },
        policy
    );
    
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();
    
    ASSERT_TRUE(result.is_success(), "Should succeed");
    ASSERT_EQ(call_count, 2, "Should retry");
    ASSERT_TRUE(elapsed_ms < 50, "Should complete quickly without backoff");
}

TEST_CASE(RetryWithBackoff_AllRetriableErrors, "Retries all Transient-severity errors") {
    // Test that all errors with Transient severity trigger retries
    int attempts_storage = 0;
    int attempts_network = 0;
    int attempts_consensus = 0;
    
    // Storage transient error
    auto result1 = retry_with_backoff<bool>(
        [&attempts_storage]() {
            attempts_storage++;
            if (attempts_storage < 2) {
                return Result<bool>(StorageErrorCode::DeserializationFailed, "Parse failed");
            }
            return Result<bool>(true);
        }, 2, 10
    );
    ASSERT_EQ(attempts_storage, 2, "IOError should trigger retry");
    
    // Network transient error
    auto result2 = retry_with_backoff<bool>(
        [&attempts_network]() {
            attempts_network++;
            if (attempts_network < 2) {
                return Result<bool>(NetworkErrorCode::MessageSendFailed, "Send failed");
            }
            return Result<bool>(true);
        }, 2, 10
    );
    ASSERT_EQ(attempts_network, 2, "ConnectionTimeout should trigger retry");
    
    // Consensus transient error
    auto result3 = retry_with_backoff<bool>(
        [&attempts_consensus]() {
            attempts_consensus++;
            if (attempts_consensus < 2) {
                return Result<bool>(ConsensusErrorCode::QuorumNotReached, "Quorum missing");
            }
            return Result<bool>(true);
        }, 2, 10
    );
    ASSERT_EQ(attempts_consensus, 2, "RoundTimeout should trigger retry");
}
