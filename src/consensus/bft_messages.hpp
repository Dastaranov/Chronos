//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file bft_messages.hpp
 * @brief This file defines the data structures for BFT (Byzantine Fault Tolerant) consensus messages.
 *
 * The messages defined here are essential for the BFT consensus protocol. They include:
 * - Prevote: A message sent by a validator to vote for a specific block.
 * - Precommit: A message sent by a validator to indicate a commitment to a block after seeing enough prevotes.
 * - NewRound: A message sent by a proposer to initiate a new round and propose a block.
 *
 * Each message structure includes fields for the consensus height, round, a block hash, the validator's identifier,
 * and a cryptographic signature to ensure authenticity and integrity. They also provide a helper method to
 * generate a canonical byte representation for signing.
 */

#pragma once

#include "util/bytes.hpp"
#include "crypto/signer.hpp"
#include <string>
#include <vector>

namespace chronos {

namespace bft {

// The Protobuf generated headers for bft_messages.proto now provide the definitions for
// Prevote, Precommit, and NewRound.
// This file is now primarily used for includes and namespace declarations.

} // namespace bft

} // namespace chronos