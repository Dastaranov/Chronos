//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file memory_kv.cpp
 * @brief This file implements the MemoryKv class, an in-memory key-value storage solution.
 *
 * The MemoryKv class provides a simple, volatile key-value store that operates entirely
 * in RAM. It implements the `IKv` interface, allowing it to be used interchangeably
 * with other storage implementations. This file contains the concrete logic for
 * storing, retrieving, and removing key-value pairs using an `std::unordered_map`.
 *
 * Key functions implemented:
 * - `MemoryKv::get`: Retrieves a value from the in-memory store.
 * - `MemoryKv::put`: Stores or updates a key-value pair.
 * - `MemoryKv::remove`: Removes a key-value pair from the store.
 */

#include "storage/memory_kv.hpp"

namespace chrono_storage {

/**
 * @brief Retrieves the value associated with a given key from the in-memory store.
 *
 * This method searches the internal `map_` for the provided `key`. If the key is found,
 * a copy of its associated value is returned wrapped in an `std::optional`. If the key
 * does not exist, `std::nullopt` is returned.
 *
 * @param key The key to look up, represented as a `chrono_util::Bytes` object.
 * @return An `std::optional<chrono_util::Bytes>` containing the value if the key is found,
 *         or `std::nullopt` if the key does not exist.
 */
std::optional<chrono_util::Bytes> MemoryKv::get(const chrono_util::Bytes& key) const {
    auto it = map_.find(key);
    if (it != map_.end()) {
        return it->second;
    }
    return std::nullopt;
}

/**
 * @brief Stores or updates a key-value pair in the in-memory store.
 *
 * This method inserts the provided `key` and `value` into the internal `map_`.
 * If the `key` already exists in the map, its associated `value` will be updated
 * with the new `value`. Otherwise, a new entry is created.
 *
 * @param key The key to store, represented as a `chrono_util::Bytes` object.
 * @param value The value to store, represented as a `chrono_util::Bytes` object.
 */
bool MemoryKv::put(const chrono_util::Bytes& key, const chrono_util::Bytes& value) {
    map_[key] = value;
    return true; // Always successful for in-memory
}

/**
 * @brief Removes a key-value pair from the in-memory store.
 *
 * This method attempts to erase the entry associated with the provided `key` from
 * the internal `map_`. If the key exists, the entry is removed. If the key does
 * not exist, the operation has no effect.
 *
 * @param key The key of the entry to remove, represented as a `chrono_util::Bytes` object.
 */
void MemoryKv::remove(const chrono_util::Bytes& key) {
    map_.erase(key);
}

} // namespace chrono_storage