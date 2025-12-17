//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_framework.hpp
 * @brief This header defines a simple, lightweight testing framework for the Chronos project.
 *
 * It provides utilities for defining test cases, registering them, and running them.
 * The framework includes assertion macros to verify conditions and report test results
 * (pass/fail) to the console. It's designed for basic unit testing within the project.
 *
 * Key components include:
 * - `TestCase` struct: Holds the name and function pointer for a single test.
 * - `get_test_cases()`: Returns a vector of all registered test cases.
 * - `register_test()`: Function to add a test case to the framework.
 * - `run_all_tests()`: Executes all registered test cases.
 * - `TEST_CASE()`: Macro for defining and automatically registering a test function.
 * - `ASSERT_TRUE()`, `ASSERT_FALSE()`, `ASSERT_EQ()`, `ASSERT_NE()`, `ASSERT_THROW()`:
 *   Macros for performing assertions within test cases.
 */

#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept> // For std::runtime_error
#include <typeinfo>  // For typeid in ASSERT_THROW
#include "util/bytes.hpp" // NEW: For chrono_util::bytes_to_hex

namespace test_framework {

/**
 * @struct TestCase
 * @brief Represents a single test case within the framework.
 *
 * Stores the name of the test and a callable function (lambda or function pointer)
 * that implements the test logic.
 */
struct TestCase {
    std::string name; ///< @var name The descriptive name of the test case.
    std::function<void()> func; ///< @var func The function object that executes the test logic.
};

/**
 * @brief Retrieves the static list of all registered test cases.
 *
 * This function provides access to the central storage for all `TestCase` objects.
 *
 * @return A reference to a `std::vector` containing all registered test cases.
 */
std::vector<TestCase>& get_test_cases();

/**
 * @brief Registers a new test case with the framework.
 *
 * This function adds a `TestCase` to the internal list, making it available
 * to be run by `run_all_tests()`.
 *
 * @param name The name of the test case.
 * @param func The function implementing the test logic.
 */
void register_test(const std::string& name, std::function<void()> func);

/**
 * @brief Executes all registered test cases.
 *
 * This function iterates through the list of registered tests, runs each one,
 * and reports the results (pass/fail) to the console. It catches exceptions
 * thrown by assertion failures to continue with other tests.
 *
 * @return The total number of failed tests.
 */
int run_all_tests();

} // namespace test_framework

/**
 * @brief Macro to define and automatically register a test case.
 *
 * This macro simplifies the creation of test functions. It declares a function,
 * defines a static `Registrar` object that automatically calls `test_framework::register_test`
 * during static initialization, and then provides the function signature for the test logic.
 *
 * Usage:
 * TEST_CASE(MyFeatureTest, "My Feature Test Display Name") {
 *     // Test logic here
 *     ASSERT_TRUE(true, "This should pass");
 * }
 */
#define TEST_CASE(identifier_name, display_name) \
    void test_##identifier_name(); \
    struct Register_##identifier_name { \
        Register_##identifier_name() { \
            test_framework::register_test(display_name, test_##identifier_name); \
        } \
    } register_##identifier_name##_instance; \
    void test_##identifier_name()

/**
 * @brief Macro similar to ASSERT_TRUE, used for preconditions that must hold.
 *        If the condition is false, it logs a failure and throws an exception.
 * @param condition The boolean condition to evaluate.
 * @param message A descriptive message for the requirement.
 */
#define REQUIRE(condition, message) ASSERT_TRUE(condition, message)

/**
 * @brief Asserts that a condition is true. If false, logs a failure and throws an exception.
 * @param condition The boolean condition to evaluate.
 * @param message A descriptive message for the assertion.
 */
#define ASSERT_TRUE(condition, message) \
    if (!(condition)) { \
        std::cerr << "  FAIL: " << message << " (Condition: " << #condition << ")" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } else { \
        std::cout << "  PASS: " << message << std::endl; \
    }

/**
 * @brief Asserts that a condition is false. Internally uses `ASSERT_TRUE` with negated condition.
 * @param condition The boolean condition to evaluate.
 * @param message A descriptive message for the assertion.
 */
#define ASSERT_FALSE(condition, message) ASSERT_TRUE(!(condition), message)

/**
 * @brief Asserts that two values are equal. If not, logs a failure and throws an exception.
 * @param expected The expected value.
 * @param actual The actual value.
 * @param message A descriptive message for the assertion.
 */
#define ASSERT_EQ(expected, actual, message) \
    if (!((expected) == (actual))) { \
        std::cerr << "  FAIL: " << message << " (Expected: " << (expected) << ", Actual: " << (actual) << ")" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } else { \
        std::cout << "  PASS: " << message << std::endl; \
    }

/**
 * @brief Asserts that two values are not equal. If they are, logs a failure and throws an exception.
 * @param expected The value that is not expected.
 * @param actual The actual value.
 * @param message A descriptive message for the assertion.
 */
#define ASSERT_NE(expected, actual, message) \
    if (!((expected) != (actual))) { \
        std::cerr << "  FAIL: " << message << " (Expected not: " << (expected) << ", Actual: " << (actual) << ")" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } else { \
        std::cout << "  PASS: " << message << std::endl; \
    }

/**
 * @brief Asserts that a specific exception type is thrown by an expression.
 *
 * If the expression does not throw an exception, or throws a different type of exception,
 * it logs a failure and throws a `std::runtime_error`.
 *
 * @param expression The code expression expected to throw an exception.
 * @param exception_type The type of exception expected to be thrown.
 * @param message A descriptive message for the assertion.
 */
#define ASSERT_THROW(expression, exception_type, message) \
    try { \
        expression; \
        std::cerr << "  FAIL: " << message << " (Expected exception " << #exception_type << " not thrown)" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } catch (const exception_type& e) { \
        std::cout << "  PASS: " << message << " (Exception caught: " << e.what() << ")" << std::endl; \
    } catch (const std::exception& e) { \
        std::cerr << "  FAIL: " << message << " (Caught wrong exception type: " << typeid(e).name() << " - " << e.what() << ")" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } catch (...) { \
        std::cerr << "  FAIL: " << message << " (Caught unknown exception type)" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    }

/**
 * @brief Asserts that two Bytes objects are equal.
 * @param expected The expected Bytes object.
 * @param actual The actual Bytes object.
 * @param message A descriptive message for the assertion.
 */
#define ASSERT_BYTES_EQ(expected, actual, message) \
    if (!((expected) == (actual))) { \
        std::cerr << "  FAIL: " << message << " (Expected: " << chrono_util::bytes_to_hex(expected) << ", Actual: " << chrono_util::bytes_to_hex(actual) << ")" << std::endl; \
        throw std::runtime_error("Assertion failed"); \
    } else { \
        std::cout << "  PASS: " << message << std::endl; \
    }
