/**
 * @file test_logging_config.cpp
 * @brief Focused tests for centralized logging bootstrap behavior.
 */

#include "test_framework.hpp"
#include "util/log.hpp"
#include "util/logging_config.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace fs = std::filesystem;

namespace {

std::string read_file(const fs::path& file_path) {
    std::ifstream input(file_path);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE(SetupLoggingCreatesDirectories, "setup_logging creates parent directories and writes INFO logs") {
    const fs::path root = fs::path("/tmp/chronos_logging_config_tests") / "info_mode";
    const fs::path log_file = root / "nested" / "chronos.log";

    if (fs::exists(root)) {
        fs::remove_all(root);
    }

    chrono_util::setup_logging(false, false, log_file.string());
    LOG_DEBUG(chrono_util::LogCategory::GENERAL, "suppressed debug message");
    LOG_INFO(chrono_util::LogCategory::GENERAL, "info message should be persisted");

    ASSERT_TRUE(fs::exists(log_file), "setup_logging should create the requested log file");

    const std::string log_contents = read_file(log_file);
    ASSERT_TRUE(log_contents.find("info message should be persisted") != std::string::npos,
                "INFO logs should be written when debug mode is disabled");
    ASSERT_FALSE(log_contents.find("suppressed debug message") != std::string::npos,
                 "DEBUG logs should be filtered out when debug mode is disabled");
}

TEST_CASE(SetupLoggingDebugMode, "setup_logging enables DEBUG logging when requested") {
    const fs::path root = fs::path("/tmp/chronos_logging_config_tests") / "debug_mode";
    const fs::path log_file = root / "chronos.log";

    if (fs::exists(root)) {
        fs::remove_all(root);
    }

    chrono_util::setup_logging(true, false, log_file.string());
    LOG_DEBUG(chrono_util::LogCategory::CONSENSUS, "debug message should be persisted");

    const std::string log_contents = read_file(log_file);
    ASSERT_TRUE(log_contents.find("debug message should be persisted") != std::string::npos,
                "DEBUG logs should be written when debug mode is enabled");
}
