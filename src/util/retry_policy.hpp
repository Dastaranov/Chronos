/**
 * @file retry_policy.hpp
 * @brief Automatic retry mechanism with exponential backoff for transient failures.
 *
 * Provides utilities for retrying operations that may fail temporarily:
 * - Exponential backoff: delay doubles each retry (100ms → 200ms → 400ms)
 * - Max retries: configurable limit (default 3 attempts)
 * - Jitter: optional randomization to prevent thundering herd
 * - Result integration: automatically retries if error is_retriable() == true
 *
 * Usage Examples:
 * @code
 * // Retry a function up to 3 times with exponential backoff
 * auto result = retry_with_backoff<int>(
 *     []() { return storage->saveBlock(block); },
 *     3,  // max_retries
 *     100 // initial_backoff_ms
 * );
 *
 * // Retry only if error is retriable (transient failures)
 * if (result.is_failure() && result.is_retriable()) {
 *     result = retry_with_backoff(...);
 * }
 *
 * // Automatic retry for Result<T>
 * auto final_result = retry_on_retriable_error(
 *     []() { return storage->saveBlock(block); },
 *     max_retries = 3
 * );
 * @endcode
 */

#pragma once

#include "error_types.hpp"
#include <functional>
#include <thread>
#include <chrono>
#include <random>
#include <stdexcept>

namespace chrono_error {

/**
 * @class RetryPolicy
 * @brief Configuration for retry behavior.
 */
struct RetryPolicy {
    int max_retries = 3;                ///< Maximum number of retry attempts (min 1)
    int initial_backoff_ms = 100;       ///< Initial backoff in milliseconds
    int max_backoff_ms = 5000;          ///< Maximum backoff duration (caps exponential growth)
    bool use_jitter = false;            ///< Add randomization to prevent thundering herd
    double backoff_multiplier = 2.0;    ///< Exponential growth factor per retry
    
    /**
     * @brief Validates policy parameters.
     * @throws std::invalid_argument if parameters are invalid
     */
    void validate() const {
        if (max_retries < 1) {
            throw std::invalid_argument("max_retries must be at least 1");
        }
        if (initial_backoff_ms < 0) {
            throw std::invalid_argument("initial_backoff_ms must be non-negative");
        }
        if (max_backoff_ms < initial_backoff_ms) {
            throw std::invalid_argument("max_backoff_ms must be >= initial_backoff_ms");
        }
        if (backoff_multiplier < 1.0) {
            throw std::invalid_argument("backoff_multiplier must be >= 1.0");
        }
    }
};

/**
 * @brief Calculates the backoff duration for a given retry attempt.
 *
 * @param attempt Zero-based attempt number (0, 1, 2, ...)
 * @param policy Retry policy with initial_backoff_ms, multiplier, max_backoff_ms
 * @return Duration to wait before next retry in milliseconds
 *
 * Formula: min(initial * (multiplier ^ attempt), max_backoff_ms)
 * Example with defaults (100ms initial, 2.0 multiplier, 5000ms max):
 * - Attempt 0: 100ms
 * - Attempt 1: 200ms
 * - Attempt 2: 400ms
 * - Attempt 3: 800ms
 * - Attempt 4: 1600ms
 * - Attempt 5: 3200ms
 * - Attempt 6+: 5000ms (capped)
 */
int calculate_backoff_ms(int attempt, const RetryPolicy& policy);

/**
 * @brief Sleeps for the calculated backoff duration.
 *
 * @param attempt Zero-based attempt number
 * @param policy Retry policy with timing parameters
 *
 * Respects the calculated backoff with optional jitter applied.
 */
void sleep_backoff(int attempt, const RetryPolicy& policy);

/**
 * @template retry_with_backoff
 * @brief Retries an operation with exponential backoff until success or max retries reached.
 *
 * @tparam T Return type of the operation
 * @param operation Callable that returns Result<T>
 * @param max_retries Maximum number of attempts (including initial)
 * @param initial_backoff_ms Initial backoff duration in milliseconds
 * @return Result<T> with success value or final error after all retries exhausted
 *
 * Retries occur if the operation returns a failure with is_retriable() == true.
 * Applies exponential backoff: 100ms, 200ms, 400ms, 800ms, etc.
 *
 * Example:
 * @code
 * Result<bool> result = retry_with_backoff<bool>(
 *     []() { return storage->saveBlock(block); },
 *     3,   // max_retries
 *     100  // initial_backoff_ms
 * );
 *
 * if (result) {
 *     LOG_INFO(STORAGE, "Block saved successfully after retries");
 * } else {
 *     LOG_ERROR(STORAGE, "Failed to save block after {} retries: {}",
 *               3, result.error_message());
 * }
 * @endcode
 */
template<typename T>
Result<T> retry_with_backoff(
    std::function<Result<T>()> operation,
    int max_retries = 3,
    int initial_backoff_ms = 100) {
    
    RetryPolicy policy;
    policy.max_retries = max_retries;
    policy.initial_backoff_ms = initial_backoff_ms;
    policy.validate();
    
    Result<T> last_result = operation();
    
    for (int attempt = 1; attempt < max_retries; ++attempt) {
        // If successful, return immediately
        if (last_result.is_success()) {
            return last_result;
        }
        
        // If not retriable (e.g., NotFound, PermissionDenied), don't retry
        if (!last_result.is_retriable()) {
            return last_result;
        }
        
        // Sleep with exponential backoff before retry
        sleep_backoff(attempt - 1, policy);
        
        // Retry the operation
        last_result = operation();
    }
    
    return last_result;
}

/**
 * @template retry_with_backoff (lambda variant)
 * @brief Retries a lambda function with exponential backoff.
 *
 * @tparam T Return type of the lambda
 * @param operation Lambda function with no parameters returning Result<T>
 * @param max_retries Maximum number of attempts
 * @param initial_backoff_ms Initial backoff in milliseconds
 * @return Result<T> with success or final error
 *
 * This overload allows passing lambda functions directly without std::function wrapping.
 */
template<typename T, typename Callable>
Result<T> retry_with_backoff(
    Callable&& operation,
    int max_retries = 3,
    int initial_backoff_ms = 100) {
    
    return retry_with_backoff<T>(
        std::function<Result<T>()>(std::forward<Callable>(operation)),
        max_retries,
        initial_backoff_ms
    );
}

/**
 * @template retry_on_error
 * @brief Retries an operation only if error is retriable (transient).
 *
 * @tparam T Return type of the operation
 * @param operation Callable returning Result<T>
 * @param policy Retry policy configuration
 * @return Result<T> with success or final error
 *
 * Different from retry_with_backoff in that it checks is_retriable() on each error.
 * Non-retriable errors (e.g., NotFound, PermissionDenied) are returned immediately.
 *
 * Example:
 * @code
 * RetryPolicy policy;
 * policy.max_retries = 5;
 * policy.initial_backoff_ms = 200;
 *
 * Result<Block> result = retry_on_error<Block>(
 *     []() { return storage->getBlock(hash); },
 *     policy
 * );
 * @endcode
 */
template<typename T>
Result<T> retry_on_error(
    std::function<Result<T>()> operation,
    const RetryPolicy& policy = RetryPolicy()) {
    
    policy.validate();
    Result<T> last_result = operation();
    
    for (int attempt = 1; attempt < policy.max_retries; ++attempt) {
        if (last_result.is_success()) {
            return last_result;
        }
        
        if (!last_result.is_retriable()) {
            return last_result;  // Non-retriable, don't retry
        }
        
        sleep_backoff(attempt - 1, policy);
        last_result = operation();
    }
    
    return last_result;
}

} // namespace chrono_error
