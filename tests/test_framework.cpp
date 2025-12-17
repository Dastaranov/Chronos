//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file test_framework.cpp
 * @brief This file implements the core functionality of the lightweight testing framework for the Chronos project.
 *
 * It provides the mechanisms for managing and executing test cases, reporting results,
 * and handling exceptions during test execution. This framework is designed to be
 * simple and effective for unit testing various components of the Chronos node.
 *
 * Key functions implemented:
 * - `get_test_cases()`: Provides access to the global list of registered test cases.
 * - `register_test()`: Adds a new test case to the list.
 * - `run_all_tests()`: Executes all registered tests and summarizes the results.
 * - `main()`: The entry point for the test runner executable.
 */

#include "test_framework.hpp"
#include <stdexcept>
#include <iostream> // For std::cout, std::cerr

namespace test_framework {

/**
 * @brief Returns a reference to the static vector of `TestCase` objects.
 *
 * This function ensures that there is a single, global list of test cases
 * that can be accessed and modified by different parts of the test suite.
 *
 * @return A reference to the `std::vector<TestCase>` containing all registered tests.
 */
std::vector<TestCase>& get_test_cases() {
    static std::vector<TestCase> test_cases; ///< @var test_cases The static vector holding all registered test cases.
    return test_cases;
}

/**
 * @brief Registers a new test case with the framework.
 *
 * This function takes a test name and a function object (e.g., a lambda or a function pointer)
 * and adds it to the global list of test cases.
 *
 * @param name The descriptive name of the test case.
 * @param func The function object that encapsulates the test logic.
 */
void register_test(const std::string& name, std::function<void()> func) {
    get_test_cases().push_back({name, func});
}

/**
 * @brief Executes all registered test cases and reports the results.
 *
 * This function iterates through each `TestCase` in the global list. For each test,
 * it attempts to execute its associated function. If the test function throws an
 * exception (indicating an assertion failure), the test is marked as failed.
 * Otherwise, it's marked as passed. A summary of passed and failed tests is printed
 * to the console at the end.
 *
 * @return The total number of failed tests. A return value of 0 indicates all tests passed.
 */
int run_all_tests() {
    int passed_count = 0; ///< @var passed_count Counter for successfully passed tests.
    int failed_count = 0; ///< @var failed_count Counter for failed tests.

    std::cout << "Running " << get_test_cases().size() << " tests..." << std::endl;

    for (const auto& test_case : get_test_cases()) {
        std::cout << "----------------------------------------" << std::endl;
        std::cout << "Running test: " << test_case.name << std::endl;
        try {
            test_case.func(); // Execute the test function
            std::cout << "Test PASSED: " << test_case.name << std::endl;
            passed_count++;
        } catch (const std::exception& e) {
            std::cerr << "Test FAILED: " << test_case.name << " - " << e.what() << std::endl;
            failed_count++;
        } catch (...) {
            std::cerr << "Test FAILED: " << test_case.name << " - Unknown exception" << std::endl;
            failed_count++;
        }
    }

    std::cout << "----------------------------------------" << std::endl;
    std::cout << "Tests finished." << std::endl;
    std::cout << "Passed: " << passed_count << std::endl;
    std::cout << "Failed: " << failed_count << std::endl;

    return failed_count;
}

} // namespace test_framework

/**
 * @brief The main entry point for the test runner executable.
 *
 * This function simply calls `test_framework::run_all_tests()` to execute all
 * registered test cases and returns the number of failed tests as the program's
 * exit code.
 *
 * @return The number of failed tests.
 */
int main() {
    return test_framework::run_all_tests();
}