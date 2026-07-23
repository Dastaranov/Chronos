#include "node/node_app.hpp"
#include "node/config.hpp"
#include "util/log.hpp"
#include "util/logging_config.hpp"
#include "util/system_lock.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    std::string config_path = "config/default.toml"; // Default config path

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    try {
        chrono_util::setup_logging(false);
        LOG_INFO(chrono_util::LogCategory::GENERAL, "Chronos node starting with config {}", config_path);

        // System-wide lock to prevent multiple instances on the same machine
        // This prevents accidental double-starts and basic Sybil attempts on one machine
        chrono_util::SystemLock lock("/tmp/chronos_guard.lock");

        // Construct Node by configfile
        chrono_node::Config cfg = chrono_node::Config::load(config_path);
        // Initialize Node
        chrono_node::NodeApp app(cfg);
        app.run();
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to start node: {}", e.what());
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}