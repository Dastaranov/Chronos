//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file console_display.hpp
 * @brief Defines the ConsoleDisplay class for a professional dashboard UI.
 *
 * This class manages a fixed-size dashboard that updates in-place without scrolling,
 * displaying node info, peer connections, blockchain metrics, and real-time logs.
 */

#pragma once

#include "node/node_status.hpp"
#include "util/log.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <iostream>

namespace chrono_util {

/**
 * @class ConsoleDisplay
 * @brief Manages a professional dashboard for the Chronos node.
 *
 * Displays:
 * - Node information (ID, ports, status, storage)
 * - Connected peers (up to 5)
 * - Blockchain metrics (height, blocks, TXs)
 * - Real-time log stream (latest 5 messages)
 * - Blockchain activity stream (latest 3 events)
 *
 * All updates happen in-place without scrolling.
 */
class ConsoleDisplay {
public:
    /**
     * @brief Constructs a ConsoleDisplay object.
     * @param status A reference to the NodeStatus object that this display will render.
     */
    explicit ConsoleDisplay(const chrono_node::NodeStatus& status);

    /**
     * @brief Initializes the dashboard layout.
     * Clears screen and draws the static structure.
     */
    void init_display();

    /**
     * @brief Updates dynamic status sections (height, blocks, TXs, mempool).
     */
    void update_status();

    /**
     * @brief Adds a log message to the log stream.
     * @param message The log message to display.
     */
    void print_log_message(const std::string& message);

private:
    const chrono_node::NodeStatus& status_;  ///< Reference to node status
    std::mutex display_mutex_;               ///< Protects console output
    std::vector<std::string> log_buffer_;    ///< Circular buffer for log messages
    size_t max_log_lines_ = 5;               ///< Max log lines to display
    int current_log_row_ = 26;               ///< Starting row for log messages
    int current_activity_row_ = 33;          ///< Starting row for activity messages

    // Helper methods for formatting
    void clear_line();
    void move_cursor_up(int lines);
    void move_cursor_to(int row, int col);
    std::string make_separator(int width);
    std::string pad_text(const std::string& text, int width, bool right_align = false);
    std::string get_timestamp();
};

} // namespace chrono_util
