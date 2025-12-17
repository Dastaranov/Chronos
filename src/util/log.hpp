//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file log.hpp
 * @brief This file defines the logging utilities for the Chronos project, including a singleton Logger class and logging macros.
 *
 * This header provides a flexible and thread-safe logging mechanism. It defines `LogLevel`
 * and `LogCategory` enums to categorize log messages by severity and origin, respectively. 
 * The `Logger` class is implemented as a singleton to ensure a single logging instance
 * across the application, writing messages to both console and a file.
 * Variadic macros are provided for convenient logging at different levels.
 *
 * Key functionalities include:
 * - `LogLevel`: Enum for log message severity (INFO, WARN, ERROR).
 * - `LogCategory`: Enum for log message categorization (GENERAL, WALLET, P2P, etc.).
 * - `Logger` class: Singleton responsible for managing log output.
 * - `LOG_INIT`: Macro to initialize the logger.
 * - `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`: Macros for logging messages with different severities.
 */

#pragma once

#include <iostream>
#include <fstream>
#include <string>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <utility> // For std::forward

namespace chrono_util {

// Forward declaration
class ConsoleDisplay;

/**
 * @enum LogLevel
 * @brief Defines the severity levels for log messages.
 */
enum class LogLevel {
    DEBUG, ///< Debug-level messages, for detailed diagnostic information.
    INFO,  ///< Informational messages, typically for tracking normal operation.
    WARN,  ///< Warning messages, indicating potential issues that are not critical.
    ERROR  ///< Error messages, indicating critical failures or unexpected behavior.
};

/**
 * @enum LogCategory
 * @brief Defines categories for log messages to help filter and understand their origin.
 */
enum class LogCategory {
    GENERAL,     ///< General application-wide messages.
    WALLET,      ///< Messages related to wallet operations.
    EPOCH,       ///< Messages related to epoch management or time-based events.
    CONSENSUS,   ///< Messages related to consensus algorithms (e.g., PoT, BFT).
    P2P,         ///< Messages related to peer-to-peer networking.
    STATE,       ///< Messages related to the ledger state.
    CRYPTO,      ///< Messages related to cryptographic operations.
    LEDGER,      ///< Messages related to ledger and block management.
    STORAGE      ///< Messages related to data storage and persistence.
};

/**
 * @class Logger
 * @brief Singleton class for managing application logging.
 *
 * The Logger class provides a centralized and thread-safe mechanism for logging
 * messages to both the console and a file. It supports different log levels and
 * categories, and ensures that log messages are properly formatted with timestamps.
 */
class Logger {
public:
    /**
     * @brief Retrieves the singleton instance of the Logger.
     *
     * @return A reference to the single Logger instance.
     */
    static Logger& get_instance();

    /**
     * @brief Initializes the logger, setting up the log file.
     *
     * This method should be called once at the application startup. It creates
     * or opens a log file in the specified directory.
     *
     * @param log_dir The directory where log files should be created. Defaults to the current directory.
     */
    void init(const std::string& log_dir = ".");

    /**
     * @brief Logs a message with a specified level, category, and content.
     *
     * This method formats the log message with a timestamp, level, and category, 
     * then writes it to both the console (stdout/stderr) and the log file.
     * It is thread-safe.
     *
     * @param level The severity level of the log message.
     * @param category The category of the log message.
     * @param message The content of the log message.
     */
    void log(LogLevel level, LogCategory category, const std::string& message);

    /**
     * @brief Sets the ConsoleDisplay instance to which log messages should also be sent.
     *
     * @param display A pointer to the ConsoleDisplay instance. Can be nullptr to disable console redirection.
     */
    void set_console_display(ConsoleDisplay* display);

private:
    /**
     * @brief Private default constructor to enforce singleton pattern.
     */
    Logger() = default;

    /**
     * @brief Private destructor to close the log file.
     */
    ~Logger();

    // Delete copy constructor and assignment operator to prevent copying
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::ofstream log_file; ///< @var log_file The output file stream for writing log messages to a file.
    std::mutex log_mutex; ///< @var log_mutex A mutex to ensure thread-safe access to the log file and console output.
    bool initialized = false; ///< @var initialized A flag indicating whether the logger has been initialized.
    ConsoleDisplay* console_display_ = nullptr; ///< @var console_display_ Pointer to the ConsoleDisplay instance for console output.

    /**
     * @brief Converts a `LogLevel` enum value to its string representation.
     *
     * @param level The `LogLevel` to convert.
     * @return A `std::string` representing the log level (e.g., "INFO", "WARN", "ERROR").
     */
    std::string level_to_string(LogLevel level);

    /**
     * @brief Converts a `LogCategory` enum value to its string representation.
     *
     * @param category The `LogCategory` to convert.
     * @return A `std::string` representing the log category (e.g., "GENERAL", "P2P").
     */
    std::string category_to_string(LogCategory category);
};

// Helper function for variadic logging
/**
 * @brief Formats arguments into a stringstream, replacing {} placeholders.
 * @param ss The stringstream to append to.
 * @param format_str The format string containing {} placeholders.
 * @param args The arguments to format.
 */
void inline format_to_string_impl(std::stringstream& ss, const std::string& format_str) {
    ss << format_str;
}

template<typename T, typename... Args>
void format_to_string_impl(std::stringstream& ss, const std::string& format_str, const T& arg, const Args&... args) {
    size_t placeholder_pos = format_str.find("{}");
    if (placeholder_pos == std::string::npos) {
        ss << format_str; // No more placeholders, append remaining format string
        // Optionally, log a warning if there are unused arguments
        // LOG_WARN(GENERAL, "Too many arguments for format string: {}", format_str);
        return;
    }

    ss << format_str.substr(0, placeholder_pos);
    ss << arg;
    format_to_string_impl(ss, format_str.substr(placeholder_pos + 2), args...);
}

template<typename... Args>
void format_to_string(std::stringstream& ss, const std::string& format_str, const Args&... args) {
    format_to_string_impl(ss, format_str, args...);
}

} // namespace chrono_util

// Macros for easy logging
/**
 * @brief Initializes the global logger instance.
 * This macro should be called once at the beginning of the application.
 * @param log_dir The directory where log files will be stored.
 */
#define LOG_INIT(log_dir) chrono_util::Logger::get_instance().init(log_dir)

/**
 * @brief Logs an informational message.
 * @param category The `LogCategory` of the message.
 * @param format_str The format string for the message.
 * @param ... Variable arguments to substitute into the format string.
 */
#define LOG_DEBUG(category, format_str, ...) do { \
    std::stringstream ss; \
    chrono_util::format_to_string(ss, format_str, ##__VA_ARGS__); \
    chrono_util::Logger::get_instance().log(chrono_util::LogLevel::DEBUG, category, ss.str()); \
} while(0)

#define LOG_INFO(category, format_str, ...) do { \
    std::stringstream ss; \
    chrono_util::format_to_string(ss, format_str, ##__VA_ARGS__); \
    chrono_util::Logger::get_instance().log(chrono_util::LogLevel::INFO, category, ss.str()); \
} while(0)

/**
 * @brief Logs a warning message.
 * @param category The `LogCategory` of the message.
 * @param format_str The format string for the message.
 * @param ... Variable arguments to substitute into the format string.
 */
#define LOG_WARN(category, format_str, ...) do { \
    std::stringstream ss; \
    chrono_util::format_to_string(ss, format_str, ##__VA_ARGS__); \
    chrono_util::Logger::get_instance().log(chrono_util::LogLevel::WARN, category, ss.str()); \
} while(0)

/**
 * @brief Logs an error message.
 * @param category The `LogCategory` of the message.
 * @param format_str The format string for the message.
 * @param ... Variable arguments to substitute into the format string.
 */
#define LOG_ERROR(category, format_str, ...) do { \
    std::stringstream ss; \
    chrono_util::format_to_string(ss, format_str, ##__VA_ARGS__); \
    chrono_util::Logger::get_instance().log(chrono_util::LogLevel::ERROR, category, ss.str()); \
} while(0)
