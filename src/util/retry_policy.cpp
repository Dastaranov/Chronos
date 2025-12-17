/**
 * @file retry_policy.cpp
 * @brief Implementation of automatic retry mechanisms with exponential backoff.
 */

#include "retry_policy.hpp"
#include <cmath>
#include <random>

namespace chrono_error {

int calculate_backoff_ms(int attempt, const RetryPolicy& policy) {
    // Calculate exponential backoff: initial * (multiplier ^ attempt)
    double backoff_double = policy.initial_backoff_ms * 
                           std::pow(policy.backoff_multiplier, attempt);
    
    // Cap at maximum backoff
    int backoff_ms = static_cast<int>(std::min(backoff_double, 
                                               static_cast<double>(policy.max_backoff_ms)));
    
    return std::max(0, backoff_ms);  // Ensure non-negative
}

void sleep_backoff(int attempt, const RetryPolicy& policy) {
    int backoff_ms = calculate_backoff_ms(attempt, policy);
    
    if (backoff_ms <= 0) {
        return;  // No sleep needed
    }
    
    int actual_sleep_ms = backoff_ms;
    
    // Apply jitter if enabled (adds randomness to prevent thundering herd)
    if (policy.use_jitter) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, backoff_ms / 2);  // ±50% variation
        int jitter = dis(gen);
        actual_sleep_ms = backoff_ms / 2 + jitter;  // Range: [backoff/2, backoff]
    }
    
    std::this_thread::sleep_for(std::chrono::milliseconds(actual_sleep_ms));
}

} // namespace chrono_error
