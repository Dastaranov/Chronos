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
    ofs.close();

    // Load existing data into cache
    auto all_data = read_all();
    for (const auto& pair : all_data) {
        cache_[pair.first] = pair.second;
    }
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
 * @brief Appends a key-value pair to the file.
 *
 * @param key The key.
 * @param value The value.
 */
void FileKv::append_to_file(const chrono_util::Bytes& key, const chrono_util::Bytes& value) {
    std::ofstream ofs(file_path_, std::ios::app);
    ofs << chrono_util::bytes_to_hex(key) << " " << chrono_util::bytes_to_hex(value) << std::endl;
}

/**
 * @brief Retrieves the value associated with a given key.
 *
 * This method checks the in-memory cache for the key.
 * It uses a `std::lock_guard` to ensure thread-safe access.
 *
 * @param key The `chrono_util::Bytes` object representing the key to search for.
 * @return An `std::optional<chrono_util::Bytes>` containing the value if the key is found,
 *         otherwise `std::nullopt`.
 */
std::optional<chrono_util::Bytes> FileKv::get(const chrono_util::Bytes& key) const {
    std::lock_guard<std::mutex> lock(mutex_); // Protect against concurrent access
    auto it = cache_.find(key);
    if (it != cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

/**
 * @brief Inserts a new key-value pair or updates an existing one.
 *
 * Updates the in-memory cache and appends the new value to the file.
 * Triggers a file rewrite (compaction) if the append count exceeds the threshold.
 * This operation is thread-safe due to the use of `std::lock_guard`.
 *
 * @param key The `chrono_util::Bytes` object representing the key.
 * @param value The `chrono_util::Bytes` object representing the value to associate with the key.
 */
bool FileKv::put(const chrono_util::Bytes& key, const chrono_util::Bytes& value) {
    std::lock_guard<std::mutex> lock(mutex_); // Protect against concurrent writes
    
    cache_[key] = value;
    append_to_file(key, value);
    append_count_++;

    if (append_count_ >= COMPACTION_THRESHOLD) {
        std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>> data(cache_.begin(), cache_.end());
        rewrite_file(data);
        append_count_ = 0;
    }

    return true;
}

/**
 * @brief Removes a key-value pair from the store.
 *
 * Removes the key from the cache and rewrites the file to ensure persistence.
 * This operation is thread-safe due to the use of `std::lock_guard`.
 *
 * @param key The `chrono_util::Bytes` object representing the key of the pair to remove.
 */
void FileKv::remove(const chrono_util::Bytes& key) {
    std::lock_guard<std::mutex> lock(mutex_); // Protect against concurrent writes
    
    if (cache_.erase(key) > 0) {
        std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>> data(cache_.begin(), cache_.end());
        rewrite_file(data);
        append_count_ = 0;
    }
}

} // namespace chrono_storage