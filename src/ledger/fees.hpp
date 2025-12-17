//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file fees.hpp
 * @brief This file defines the FeePolicy structure for calculating transaction fees in the Chronos ledger.
 *
 * The FeePolicy structure provides a mechanism to define and calculate the fees associated with
 * transactions. This is a crucial component for managing network economics and preventing spam.
 *
 * Key functionalities include:
 * - `FeePolicy`: A structure to hold fee-related parameters.
 * - `calc(const Tx& tx)`: A method to calculate the fee for a given transaction.
 */

#pragma once
#include "ledger/transaction.hpp"

/**
 * @struct FeePolicy
 * @brief Defines the policy for calculating transaction fees.
 *
 * This structure holds parameters related to transaction fees and provides a method
 * to calculate the fee for a given transaction. The `base_fee` is currently a fixed value.
 */
struct FeePolicy {
  uint64_t base_fee{1000}; ///< @var base_fee The base transaction fee in nanos (e.g., 1000 nanos).
  
  /**
   * @brief Calculates the transaction fee for a given transaction.
   *
   * This method currently returns a fixed `base_fee` for any transaction.
   * In a more advanced implementation, this could take into account factors
   * like transaction size, complexity, network congestion, etc.
   *
   * @param tx The transaction for which to calculate the fee.
   * @return The calculated transaction fee in nanos.
   */
  uint64_t calc(const Tx& tx) const { (void)tx; return base_fee; }
};