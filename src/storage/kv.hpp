//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file kv.hpp
 * @brief This file defines an older or alternative IKv interface for key-value storage.
 *
 * NOTE: This file appears to define an alternative or older version of the `IKv` interface.
 * The primary `IKv` interface is defined in `src/storage/IKv.hpp` and uses `chrono_util::Bytes`
 * for keys and values, and includes a `remove` method. This version uses `std::string` for keys
 * and `std::vector<uint8_t>` for values, and lacks a `remove` method.
 *
 * This interface provides a contract for basic key-value storage operations.
 *
 * Key functionalities include:
 * - `put(const std::string& k, const std::vector<uint8_t>& v)`: Stores a key-value pair.
 * - `get(const std::string& k)`: Retrieves the value associated with a given key.
 * - `make_memory_kv()`: A factory function to create an in-memory key-value store.
 */

#pragma once
#include <string>
#include <vector>
#include <optional>

/**
 * @class IKv
 * @brief Abstract base class defining an interface for key-value storage.
 *
 * This interface provides a standardized way to interact with different key-value
 * storage mechanisms. Implementations should provide methods for storing and
 * retrieving byte vectors associated with string keys.
 *
 * NOTE: This interface is less complete than `src/storage/IKv.hpp` and might be
 * an older version or intended for a different purpose.
 */
class IKv {
public:
  /**
   * @brief Virtual destructor for the IKv interface.
   *
   * Ensures proper cleanup of resources for derived classes when an IKv pointer
   * is deleted.
   */
  virtual ~IKv()=default;

  /**
   * @brief Pure virtual function to store a key-value pair.
   *
   * Derived classes must implement this method to persist data to the underlying storage.
   * If the key already exists, its associated value should be updated.
   *
   * @param k The key to store, represented as a `std::string`.
   * @param v The value to store, represented as a `std::vector<uint8_t>`.
   */
  virtual void put(const std::string& k, const std::vector<uint8_t>& v)=0;

  /**
   * @brief Pure virtual function to retrieve the value associated with a given key.
   *
   * Derived classes must implement this method to fetch data from the underlying storage.
   *
   * @param k The key to look up, represented as a `std::string`.
   * @return An `std::optional<std::vector<uint8_t>>` containing the value if the key is found,
   *         or `std::nullopt` if the key does not exist.
   */
  virtual std::optional<std::vector<uint8_t>> get(const std::string& k)=0;
};

/**
 * @brief Factory function to create an in-memory key-value store.
 *
 * This function returns a pointer to an `IKv` implementation that stores data
 * in memory. This is typically used for testing or temporary storage.
 *
 * @return A pointer to an `IKv` object representing an in-memory key-value store.
 */
IKv* make_memory_kv();
