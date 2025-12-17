//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file console_display.cpp
 * @brief Implements a professional dashboard for the Chronos node with real-time status updates.
 *
 * This implementation provides a structured, fixed-size dashboard that displays:
 * - Node information (ID, addresses, status)
 * - Connected peers list
 * - System health metrics (storage, ports)
 * - Real-time log stream
 * - Blockchain activity stream
 *
 * All updates happen in-place without scrolling, using ANSI escape codes.
 */

#include "util/console_display.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <ctime>
#include <chrono>
#include <algorithm>

namespace chrono_util {

// ANSI escape codes for colors and formatting
const std::string CLEAR_SCREEN = "\033[2J";
const std::string CURSOR_HOME = "\033[H";
const std::string CLEAR_LINE = "\033[2K";
const std::string SAVE_CURSOR = "\033[s";
const std::string RESTORE_CURSOR = "\033[u";
const std::string HIDE_CURSOR = "\033[?25l";
const std::string SHOW_CURSOR = "\033[?25h";

// Colors
const std::string COLOR_BOLD = "\033[1m";
const std::string COLOR_DIM = "\033[2m";
const std::string COLOR_GREEN = "\033[32m";
const std::string COLOR_YELLOW = "\033[33m";
const std::string COLOR_CYAN = "\033[36m";
const std::string COLOR_RESET = "\033[0m";
const std::string COLOR_BG_DARK = "\033[40m";

// Box drawing characters (Unicode box-drawing)
const std::string HLINE = "─";
const std::string VLINE = "│";
const std::string TL_CORNER = "┌";
const std::string TR_CORNER = "┐";
const std::string BL_CORNER = "└";
const std::string BR_CORNER = "┘";

/**
 * @brief Constructs a ConsoleDisplay object with dashboard initialization.
 */
ConsoleDisplay::ConsoleDisplay(const chrono_node::NodeStatus& status)
    : status_(status) {
    std::atexit([]() { std::cout << SHOW_CURSOR << std::flush; });
}

void ConsoleDisplay::clear_line() {
    std::cout << CLEAR_LINE << "\r";
}

void ConsoleDisplay::move_cursor_up(int lines) {
    if (lines > 0) {
        std::cout << "\033[" << lines << "A";
    }
}

void ConsoleDisplay::move_cursor_to(int row, int col) {
    std::cout << "\033[" << row << ";" << col << "H";
}

/**
 * @brief Creates a horizontal separator line of specified width.
 */
std::string ConsoleDisplay::make_separator(int width) {
    return std::string(width, '-');
}

/**
 * @brief Pads text to a specific width (left-aligned by default).
 */
std::string ConsoleDisplay::pad_text(const std::string& text, int width, bool right_align) {
    std::string result = text;
    if (result.length() < width) {
        int padding = width - result.length();
        if (right_align) {
            result = std::string(padding, ' ') + result;
        } else {
            result = result + std::string(padding, ' ');
        }
    }
    return result;
}

/**
 * @brief Returns timestamp string in HH:MM:SS format.
 */
std::string ConsoleDisplay::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto tm = *std::localtime(&time);
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << tm.tm_hour << ":"
        << std::setw(2) << tm.tm_min << ":"
        << std::setw(2) << tm.tm_sec;
    return oss.str();
}

/**
 * @brief Initializes the dashboard display layout.
 */
void ConsoleDisplay::init_display() {
    std::lock_guard<std::mutex> lock(display_mutex_);
    std::cout << CLEAR_SCREEN << CURSOR_HOME << HIDE_CURSOR;

    // Dashboard width
    const int WIDTH = 120;

    // === HEADER ===
    std::cout << COLOR_BOLD << COLOR_CYAN;
    std::cout << "╔" << make_separator(WIDTH - 2) << "╗" << std::endl;
    std::cout << "║" << pad_text(" CHRONOS BLOCKCHAIN NODE DASHBOARD", WIDTH - 2) << "║" << std::endl;
    std::cout << "╚" << make_separator(WIDTH - 2) << "╝" << std::endl;
    std::cout << COLOR_RESET;

    // === NODE INFORMATION SECTION (Lines 5-12) ===
    std::cout << COLOR_BOLD << " ▸ NODE INFORMATION" << COLOR_RESET << std::endl;
    std::cout << "  ├─ Node ID:          " << status_.node_id.substr(0, 40) << "..." << std::endl;
    std::cout << "  ├─ RPC Port:         " << status_.rpc_address << std::endl;
    std::cout << "  ├─ P2P Address:      " << status_.p2p_address << std::endl;
    std::cout << "  ├─ Status:           " << COLOR_GREEN << "RUNNING" << COLOR_RESET << std::endl;
    std::cout << "  ├─ Data Directory:   " << status_.data_dir << " " << COLOR_GREEN << "[OK]" << COLOR_RESET << std::endl;
    std::cout << "  └─ Storage Size:     " << status_.storage_size << std::endl;

    // === CONNECTED PEERS SECTION (Lines 13-18) ===
    std::cout << std::endl << COLOR_BOLD << " ▸ CONNECTED PEERS (" << status_.connected_peers << ")" << COLOR_RESET << std::endl;
    std::cout << "  ├─ [No peers connected yet]" << std::endl;
    std::cout << "  ├─ " << std::endl;
    std::cout << "  ├─ " << std::endl;
    std::cout << "  └─ " << std::endl;

    // === BLOCKCHAIN METRICS SECTION (Lines 19-24) ===
    std::cout << std::endl << COLOR_BOLD << " ▸ BLOCKCHAIN METRICS" << COLOR_RESET << std::endl;
    std::cout << "  ├─ Current Height:   " << status_.current_block_height << std::endl;
    std::cout << "  ├─ Total Blocks:     " << status_.total_blocks_processed << std::endl;
    std::cout << "  ├─ Total TXs:        " << status_.total_transactions_processed << std::endl;
    std::cout << "  └─ Mempool Size:     " << status_.mempool_size << " transactions" << std::endl;

    // === REAL-TIME LOG STREAM (Lines 25-31) ===
    std::cout << std::endl << COLOR_BOLD << " ▸ LOG STREAM (Latest 5)" << COLOR_RESET << std::endl;
    for (int i = 0; i < 5; i++) {
        std::cout << "  │ " << std::endl;
    }

    // === BLOCKCHAIN ACTIVITY STREAM (Lines 32-37) ===
    std::cout << std::endl << COLOR_BOLD << " ▸ BLOCKCHAIN ACTIVITY (Latest 3)" << COLOR_RESET << std::endl;
    std::cout << "  │ " << std::endl;
    std::cout << "  │ " << std::endl;
    std::cout << "  └─ " << std::endl;

    // === FOOTER ===
    std::cout << COLOR_DIM << std::endl << "  [CTRL+C to stop]" << COLOR_RESET << std::endl;

    std::cout << std::flush;
    current_log_row_ = 26; // Starting row for log messages
    current_activity_row_ = 33; // Starting row for activity messages
}

/**
 * @brief Updates dynamic status sections in-place.
 */
void ConsoleDisplay::update_status() {
    std::lock_guard<std::mutex> lock(display_mutex_);

    // Update Node Status Line (RPC, P2P)
    move_cursor_to(7, 1);
    std::cout << "  ├─ RPC Port:         " << pad_text(status_.rpc_address, 30) << "  " << std::endl;

    // Update Blockchain Metrics
    move_cursor_to(20, 1);
    std::cout << "  ├─ Current Height:   " << pad_text(std::to_string(status_.current_block_height), 30) << "  " << std::endl;
    move_cursor_to(21, 1);
    std::cout << "  ├─ Total Blocks:     " << pad_text(std::to_string(status_.total_blocks_processed), 30) << "  " << std::endl;
    move_cursor_to(22, 1);
    std::cout << "  ├─ Total TXs:        " << pad_text(std::to_string(status_.total_transactions_processed), 30) << "  " << std::endl;
    move_cursor_to(23, 1);
    std::cout << "  └─ Mempool Size:     " << pad_text(std::to_string(status_.mempool_size) + " transactions", 30) << "  " << std::endl;

    std::cout << std::flush;
}

/**
 * @brief Adds a log message to the log stream (circular buffer).
 */
void ConsoleDisplay::print_log_message(const std::string& message) {
    std::lock_guard<std::mutex> lock(display_mutex_);

    // Truncate long messages
    std::string display_msg = message.length() > 100 ? message.substr(0, 97) + "..." : message;
    display_msg = "[" + get_timestamp() + "] " + display_msg;

    // Add to buffer
    log_buffer_.push_back(display_msg);
    if (log_buffer_.size() > max_log_lines_) {
        log_buffer_.erase(log_buffer_.begin());
    }

    // Update log section
    move_cursor_to(current_log_row_, 1);
    for (size_t i = 0; i < max_log_lines_; ++i) {
        std::cout << "  │ " << COLOR_DIM << pad_text((i < log_buffer_.size()) ? log_buffer_[i] : "", 110) << COLOR_RESET << std::endl;
    }

    std::cout << std::flush;
}

} // namespace chrono_util
