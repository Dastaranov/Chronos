//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file log.cpp
 * @brief This file implements the Logger class, providing a comprehensive logging utility for the Chronos project.
 *
 * This implementation provides a thread-safe singleton logger that outputs messages
 * to both the console and a daily rotating log file. It supports different log levels
 * (INFO, WARN, ERROR) and categories (GENERAL, WALLET, P2P, etc.) to facilitate
 * debugging and monitoring of the application.
 *
 * Key functions implemented:
 * - `Logger::get_instance()`: Retrieves the singleton instance of the Logger.
 * - `Logger::init()`: Initializes the logger and sets up the log file.
 * - `Logger::log()`: Formats and writes log messages to output.
 * - `Logger::level_to_string()`: Converts LogLevel enum to string.
 * - `Logger::category_to_string()`: Converts LogCategory enum to string.
 */

#include "util/log.hpp"
#include "util/console_display.hpp" // Include ConsoleDisplay
#include <stdexcept>
#include <ctime>

namespace chrono_util {

/**
 * @brief Retrieves the singleton instance of the Logger.
 *
 * This static method ensures that only one instance of the Logger class exists
 * throughout the application's lifetime.
 *
 * @return A reference to the single Logger instance.
 */
Logger& Logger::get_instance() {
    static Logger instance; ///< @var instance The static instance of the Logger.
    return instance;
}

/**
 * @brief Destructor for the Logger class.
 *
 * Ensures that the log file stream is properly closed when the Logger instance
 * is destroyed, preventing resource leaks.
 */
Logger::~Logger() {
    if (log_file.is_open()) {
        log_file.close();
    }
}

/**
 * @brief Initializes the logger, setting up the log file.
 *
 * This method should be called once at the application startup. It constructs a log
 * file name based on the current date and the provided `log_dir`, then attempts to
 * open the file in append mode. If file opening fails, it falls back to console output.
 *
 * @param log_dir The directory where log files should be created. Defaults to the current directory.
 */
void Logger::init(const std::string& log_dir) {
    if (initialized) {
        return;
    }

    auto now = std::chrono::system_clock::now(); ///< @var now Current system time point.
    std::time_t time = std::chrono::system_clock::to_time_t(now); ///< @var time Current system time as `std::time_t`.
    std::tm tm = *std::localtime(&time); ///< @var tm Local time structure for formatting.

    std::stringstream ss; ///< @var ss Stringstream to build the log file path.
    ss << log_dir << "/log_" << std::put_time(&tm, "%Y-%m-%d") << ".txt";
    
    log_file.open(ss.str(), std::ios::out | std::ios::app);
    if (!log_file.is_open()) {
        // Fallback to console if file opening fails
        std::cerr << "Failed to open log file: " << ss.str() << std::endl;
        // Log this event even if file logging is not available
        std::cerr << "Logger: Log file could not be opened. Falling back to console only." << std::endl;
    } else {
        std::cout << "Logger: Log file opened successfully: " << ss.str() << std::endl;
    }

    initialized = true;
}

/**
 * @brief Sets the ConsoleDisplay instance to which log messages should also be sent.
 *
 * @param display A pointer to the ConsoleDisplay instance. Can be nullptr to disable console redirection.
 */
void Logger::set_console_display(ConsoleDisplay* display) {
    console_display_ = display;
}

/**
 * @brief Logs a message with a specified level, category, and content.
 *
 * This method formats the log message with a precise timestamp (including milliseconds),
 * log level, and category. It then writes the formatted message to both the console
 * (using `std::cout` for INFO/WARN, `std::cerr` for ERROR) and the opened log file.
 * A `std::lock_guard` ensures thread-safe access to the output streams.
 *
 * @param level The severity level of the log message.
 * @param category The category of the log message.
 * @param message The content of the log message.
 */
void Logger::log(LogLevel level, LogCategory category, const std::string& message) {
    if (!initialized) {
        // Fallback to console if not initialized
        std::cerr << "Logger not initialized. Message: " << message << std::endl;
        return;
    }

    auto now = std::chrono::system_clock::now(); ///< @var now Current system time point.
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000; ///< @var ms Milliseconds part of the current time.
    std::time_t time = std::chrono::system_clock::to_time_t(now); ///< @var time Current system time as `std::time_t`.
    std::tm tm = *std::localtime(&time); ///< @var tm Local time structure for formatting.

    std::stringstream log_stream; ///< @var log_stream Stringstream to build the complete log message.
    log_stream << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    log_stream << '.' << std::setfill('0') << std::setw(3) << ms.count();
    log_stream << " [" << level_to_string(level) << "]";
    log_stream << " [" << category_to_string(category) << "] ";
    log_stream << message;

    std::lock_guard<std::mutex> guard(log_mutex); ///< @var guard A lock guard to protect shared resources during logging.
    
    // Write to console
    // std::ostream& console_stream = (level == LogLevel::ERROR) ? std::cerr : std::cout;
    // console_stream << log_stream.str() << std::endl;

    // Redirect to ConsoleDisplay if set
    if (console_display_) {
        console_display_->print_log_message(log_stream.str());
    } else {
        // Fallback to standard console output if ConsoleDisplay is not set
        std::ostream& console_stream = (level == LogLevel::ERROR) ? std::cerr : std::cout;
        console_stream << log_stream.str() << std::endl;
    }

    // Write to file
    if (log_file.is_open()) {
        log_file << log_stream.str() << std::endl;
    }
}

/**
 * @brief Converts a `LogLevel` enum value to its string representation.
 *
 * @param level The `LogLevel` to convert.
 * @return A `std::string` representing the log level (e.g., "INFO", "WARN", "ERROR").
 */
std::string Logger::level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO: return "INFO";
        case LogLevel::WARN: return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

/**
 * @brief Converts a `LogCategory` enum value to its string representation.
 *
 * @param category The `LogCategory` to convert.
 * @return A `std::string` representing the log category (e.g., "GENERAL", "P2P").
 */
std::string Logger::category_to_string(LogCategory category) {
    switch (category) {
        case LogCategory::GENERAL: return "GENERAL";
        case LogCategory::WALLET: return "WALLET";
        case LogCategory::EPOCH: return "EPOCH";
        case LogCategory::CONSENSUS: return "CONSENSUS";
        case LogCategory::P2P: return "P2P";
        case LogCategory::STATE: return "STATE";
        case LogCategory::CRYPTO: return "CRYPTO";
        case LogCategory::LEDGER: return "LEDGER";
        default: return "UNKNOWN";
    }
}

} // namespace chrono_util