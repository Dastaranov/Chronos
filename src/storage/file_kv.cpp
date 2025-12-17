//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file file_kv.cpp
 * @brief This file implements the FileKv class, providing a simple file-based key-value store.
 *
 * The FileKv class offers basic key-value storage functionality where data is persisted
 * in a plain text file. Each key-value pair is stored on a new line, with the key and
 * value represented in hexadecimal format and separated by a space.
 *
 * This implementation is suitable for small-scale, simple persistence needs and
 * includes basic concurrency protection using a mutex for write operations.
 *
 * Key functions implemented:
 * - `FileKv::FileKv`: Constructor to initialize the store with a file path.
 * - `FileKv::~FileKv`: Default destructor.
 * - `FileKv::read_all`: Reads all key-value pairs from the file.
 * - `FileKv::rewrite_file`: Rewrites the entire file with a given set of key-value pairs.
 * - `FileKv::get`: Retrieves a value associated with a given key.
 * - `FileKv::put`: Inserts or updates a key-value pair.
 * - `FileKv::remove`: Removes a key-value pair.
 */

#include "storage/file_kv.hpp"
#include "util/bytes.hpp"
#include <fstream>
#include <vector>
#include <optional> // Required for std::optional

namespace chrono_storage {

/**
 * @brief Constructs a FileKv object, initializing the key-value store with a specified file.
 *
 * The constructor takes a file path and ensures that the file exists. If the file does not
 * exist, it will be created. This operation uses `std::ios::app` to open the file, which
 * creates it if it doesn't exist without truncating its content if it does.
 *
 * @param file_path The path to the file that will serve as the key-value store.
 */
FileKv::FileKv(const std::string& file_path) : file_path_(file_path) {
    // Ensure the file exists by opening it in append mode, which creates it if it doesn't.
    std::ofstream ofs(file_path_, std::ios::app);
}

/**
 * @brief Default destructor for the FileKv class.
 *
 * Cleans up any resources held by the FileKv instance.
 */
FileKv::~FileKv() = default;

/**
 * @brief Reads all key-value pairs from the storage file.
 *
 * This method reads the entire content of the file, parsing each line into a key-value pair.
 * Each line is expected to contain a hexadecimal key, followed by a space, and then a
 * hexadecimal value. Empty lines are skipped.
 *
 * @return A `std::vector` of `std::pair<chrono_util::Bytes, chrono_util::Bytes>`
 *         containing all key-value pairs found in the file.
 */
std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>> FileKv::read_all() const {
    std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>> data;
    std::ifstream ifs(file_path_);
    std::string line;
    while (std::getline(ifs, line)) {
        if (line.empty()) continue; // Skip empty lines
        std::string key_hex, value_hex;
        size_t space_pos = line.find(' ');
        if (space_pos != std::string::npos) {
            key_hex = line.substr(0, space_pos);
            value_hex = line.substr(space_pos + 1);
            data.emplace_back(chrono_util::hex_to_bytes(key_hex), chrono_util::hex_to_bytes(value_hex));
        }
    }
    return data;
}

/**
 * @brief Rewrites the entire storage file with the provided key-value pairs.
 *
 * This method truncates the existing file and then writes all the given key-value pairs
 * to it. Each pair is written on a new line, with the key and value converted to
 * hexadecimal strings and separated by a space.
 *
 * @param data A `std::vector` of `std::pair<chrono_util::Bytes, chrono_util::Bytes>`
 *             representing the new set of key-value pairs to write to the file.
 */
void FileKv::rewrite_file(const std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>>& data) {
    // Open in truncate mode to clear existing content
    std::ofstream ofs(file_path_, std::ios::trunc);
    for (const auto& pair : data) {
        ofs << chrono_util::bytes_to_hex(pair.first) << " " << chrono_util::bytes_to_hex(pair.second) << std::endl;
    }
}

/**
 * @brief Retrieves the value associated with a given key.
 *
 * This method reads all key-value pairs from the file and searches for the specified key.
 * It uses a `std::lock_guard` to ensure thread-safe access to the file during the read operation.
 *
 * @param key The `chrono_util::Bytes` object representing the key to search for.
 * @return An `std::optional<chrono_util::Bytes>` containing the value if the key is found,
 *         otherwise `std::nullopt`.
 */
std::optional<chrono_util::Bytes> FileKv::get(const chrono_util::Bytes& key) const {
    std::lock_guard<std::mutex> lock(mutex_); // Protect against concurrent writes
    auto all_data = read_all();
    for (const auto& pair : all_data) {
        if (pair.first == key) {
            return pair.second;
        }
    }
    return std::nullopt;
}

/**
 * @brief Inserts a new key-value pair or updates an existing one.
 *
 * If the key already exists in the store, its corresponding value is updated.
 * If the key does not exist, a new key-value pair is added.
 * This operation is thread-safe due to the use of `std::lock_guard`.
 *
 * @param key The `chrono_util::Bytes` object representing the key.
 * @param value The `chrono_util::Bytes` object representing the value to associate with the key.
 */
bool FileKv::put(const chrono_util::Bytes& key, const chrono_util::Bytes& value) {
    std::lock_guard<std::mutex> lock(mutex_); // Protect against concurrent writes
    auto all_data = read_all();
    bool found = false;
    for (auto& pair : all_data) {
        if (pair.first == key) {
            pair.second = value; // Update existing value
            found = true;
            break;
        }
    }
    if (!found) {
        all_data.emplace_back(key, value); // Add new key-value pair
    }
    rewrite_file(all_data); // Rewrite the entire file with updated data
    return true; // Always successful for this simple implementation
}

/**
 * @brief Removes a key-value pair from the store.
 *
 * This method reads all key-value pairs, filters out the one matching the specified key,
 * and then rewrites the file with the remaining data.
 * This operation is thread-safe due to the use of `std::lock_guard`.
 *
 * @param key The `chrono_util::Bytes` object representing the key of the pair to remove.
 */
void FileKv::remove(const chrono_util::Bytes& key) {
    std::lock_guard<std::mutex> lock(mutex_); // Protect against concurrent writes
    auto all_data = read_all();
    std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>> new_data;
    for (const auto& pair : all_data) {
        if (pair.first != key) {
            new_data.push_back(pair); // Keep pairs that do not match the key
        }
    }
    rewrite_file(new_data); // Rewrite the file without the removed pair
}

} // namespace chrono_storage