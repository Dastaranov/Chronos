//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file logging_config.hpp
 * @brief Central logging bootstrap helpers for Chronos executables and tests.
 */

#pragma once

#include "util/log.hpp"
#include <filesystem>
#include <optional>
#include <string>

namespace chrono_util {

/**
 * @brief Configures the global Chronos logger with file output and optional console mirroring.
 *
 * The parent directory for the configured log file is created automatically when needed.
 * Console output defaults to the selected debug mode unless explicitly overridden.
 *
 * @param debug Enables DEBUG-level logging when true, otherwise INFO-level logging.
 * @param enable_console Optional explicit console toggle. When omitted, it follows `debug`.
 * @param log_file Full path to the log file that should receive log output.
 */
inline void setup_logging(
    bool debug = false,
    std::optional<bool> enable_console = std::nullopt,
    const std::string& log_file = "logs/chronos.log"
) {
    const bool console = enable_console.has_value() ? *enable_console : debug;
    const std::filesystem::path log_path(log_file);

    if (log_path.has_parent_path()) {
        std::filesystem::create_directories(log_path.parent_path());
    }

    Logger::get_instance().configure(
        log_path.string(),
        console,
        debug ? LogLevel::DEBUG : LogLevel::INFO
    );
}

} // namespace chrono_util
