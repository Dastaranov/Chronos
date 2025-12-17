//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file IKv.hpp
 * @brief This file defines the IKv interface, an abstract base class for key-value storage.
 *
 * The IKv interface establishes a contract for any class that provides key-value storage
 * functionalities. It ensures a consistent API for different underlying storage implementations
 * (e.g., in-memory, disk-based, database). This interface is crucial for abstracting
 * data persistence mechanisms within the Chronos project.
 *
 * Key functionalities defined by this interface:
 * - `get(const chrono_util::Bytes& key)`: Retrieves the value associated with a given key.
 * - `put(const chrono_util::Bytes& key, const chrono_util::Bytes& value)`: Stores a key-value pair.
 * - `remove(const chrono_util::Bytes& key)`: Removes a key-value pair.
 */

#pragma once

#include "util/bytes.hpp"
#include <optional>
#include <string>

namespace chrono_storage {

/**
 * @class IKv
 * @brief Abstract base class defining the interface for key-value storage.
 *
 * This interface provides a standardized way to interact with different key-value
 * storage mechanisms. Concrete implementations of this interface will handle the
 * specifics of data persistence, retrieval, and deletion.
 */
class IKv {
public:
    /**
     * @brief Virtual destructor for the IKv interface.
     *
     * Ensures proper cleanup of resources for derived classes when an IKv pointer
     * is deleted.
     */
    virtual ~IKv() = default;

    /**
     * @brief Pure virtual function to retrieve the value associated with a given key.
     *
     * Derived classes must implement this method to fetch data from the underlying storage.
     *
     * @param key The key to look up, represented as a `chrono_util::Bytes` object.
     * @return An `std::optional<chrono_util::Bytes>` containing the value if the key is found,
     *         or `std::nullopt` if the key does not exist.
     */
    virtual std::optional<chrono_util::Bytes> get(const chrono_util::Bytes& key) const = 0;

    /**
     * @brief Pure virtual function to store a key-value pair.
     *
     * Derived classes must implement this method to persist data to the underlying storage.
     * If the key already exists, its associated value should be updated.
     *
     * @param key The key to store, represented as a `chrono_util::Bytes` object.
     * @param value The value to store, represented as a `chrono_util::Bytes` object.
     */
    virtual bool put(const chrono_util::Bytes& key, const chrono_util::Bytes& value) = 0;

    /**
     * @brief Pure virtual function to remove a key-value pair.
     *
     * Derived classes must implement this method to delete data from the underlying storage.
     *
     * @param key The key of the entry to remove, represented as a `chrono_util::Bytes` object.
     */
    virtual void remove(const chrono_util::Bytes& key) = 0;
};

} // namespace chrono_storage