//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file types.hpp
 * @brief This file defines common type aliases used within the Chronos ledger module.
 *
 * This header centralizes the definitions of fundamental data types, such as `Amount`,
 * to improve code readability, maintainability, and to provide a single point of
 * modification if the underlying representation of these types needs to change.
 *
 * Key functionalities include:
 * - `Amount`: A type alias for `uint64_t` representing monetary values in nanos.
 */

#pragma once
#include <cstdint>
#include <string>

/**
 * @brief Type alias for representing monetary amounts in the Chronos ledger.
 *
 * `Amount` is defined as a `uint64_t` to store values in "nanos" (the smallest
 * divisible unit of the Chronos currency). This provides a consistent way to
 * handle currency values throughout the system.
 */
using Amount = uint64_t;   // nanos
