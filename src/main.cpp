#include "node/node_app.hpp"
#include "node/config.hpp"
#include "util/log.hpp"
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

    // Initialize logging
    LOG_INIT(".");

    try {
        // Construct Node by configfile
        chrono_node::Config cfg = chrono_node::Config::load(config_path);
        // Initialize Node
        chrono_node::NodeApp app(cfg);
        app.run();
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::GENERAL, "Failed to start node: {}", e.what());
        return 1;
    }

    return 0;
}