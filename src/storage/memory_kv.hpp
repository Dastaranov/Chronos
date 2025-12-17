//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file memory_kv.hpp
 * @brief This file defines the MemoryKv class, an in-memory implementation of the IKv key-value storage interface.
 *
 * The MemoryKv class provides a simple, volatile key-value store that operates entirely
 * in RAM. It is suitable for testing, caching, or scenarios where data persistence
 * across application restarts is not required. It implements the `IKv` interface,
 * allowing it to be used interchangeably with other storage implementations.
 *
 * Key functionalities include:
 * - `get(const chrono_util::Bytes& key)`: Retrieves a value from the in-memory store.
 * - `put(const chrono_util::Bytes& key, const chrono_util::Bytes& value)`: Stores or updates a key-value pair.
 * - `remove(const chrono_util::Bytes& key)`: Removes a key-value pair from the store.
 */

#pragma once

#include "storage/IKv.hpp"
#include <unordered_map>

namespace chrono_storage {

/**
 * @class MemoryKv
 * @brief An in-memory implementation of the IKv key-value storage interface.
 *
 * This class provides a volatile key-value store using an `std::unordered_map`.
 * Data stored in `MemoryKv` is lost when the application terminates. It is designed
 * for fast access and is useful for temporary data storage or testing purposes.
 */
class MemoryKv : public IKv {
public:
    /**
     * @brief Retrieves the value associated with a given key from the in-memory store.
     *
     * @param key The key to look up, represented as a `chrono_util::Bytes` object.
     * @return An `std::optional<chrono_util::Bytes>` containing the value if the key is found,
     *         or `std::nullopt` if the key does not exist.
     */
    std::optional<chrono_util::Bytes> get(const chrono_util::Bytes& key) const override;

    /**
     * @brief Stores or updates a key-value pair in the in-memory store.
     *
     * If the key already exists, its associated value is updated. Otherwise, a new
     * key-value pair is inserted.
     *
     * @param key The key to store, represented as a `chrono_util::Bytes` object.
     * @param value The value to store, represented as a `chrono_util::Bytes` object.
     */
    bool put(const chrono_util::Bytes& key, const chrono_util::Bytes& value) override;

    /**
     * @brief Removes a key-value pair from the in-memory store.
     *
     * If the key exists, its associated entry is removed from the map.
     *
     * @param key The key of the entry to remove, represented as a `chrono_util::Bytes` object.
     */
    void remove(const chrono_util::Bytes& key) override;

private:
    // The unordered_map requires a custom hash function for chrono_util::Bytes
    std::unordered_map<chrono_util::Bytes, chrono_util::Bytes, chrono_util::BytesHasher> map_; ///< @var map_ The underlying `std::unordered_map` used to store key-value pairs.
};

} // namespace chrono_storage