//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file peer_manager.hpp
 * @brief This file defines the PeerManager class, responsible for managing a node's connections to other peers.
 *
 * The PeerManager class maintains a list of known peer addresses and provides thread-safe
 * operations for adding, removing, and retrieving peer information. It is a crucial component
 * for maintaining network connectivity and enabling peer discovery within the Chronos network.
 *
 * Key functionalities include:
 * - `PeerManager()`: Constructor to initialize the peer manager.
 * - `add_peer(const std::string& peer_address)`: Adds a new peer to the manager's list.
 * - `remove_peer(const std::string& peer_address)`: Removes a peer from the list.
 * - `get_peers()`: Retrieves a list of all known peers.
 * - `get_random_peer()`: Selects a random peer from the list.
 */

#pragma once

#include <string>
#include <vector>
#include <set>
#include <mutex>
#include <random> // For std::default_random_engine, std::uniform_int_distribution
#include <algorithm> // For std::shuffle

#include "util/log.hpp"

namespace chrono_p2p {

/**
 * @class PeerManager
 * @brief Manages a collection of known peer addresses in a thread-safe manner.
 *
 * This class is responsible for keeping track of other nodes in the network.
 * It provides mechanisms to add and remove peers, retrieve the full list of peers,
 * and select a random peer for connection or communication purposes.
 * All operations are designed to be thread-safe to support concurrent access.
 */
class PeerManager {
public:
    /**
     * @brief Constructs a PeerManager object.
     *
     * Initializes the internal data structures for storing peers and the random
     * number generator for selecting random peers.
     */
    PeerManager();

    /**
     * @brief Adds a new peer address to the manager.
     *
     * This method adds a peer's address (typically in "ip:port" format) to the
     * internal set of known peers. It ensures that duplicate entries are not added.
     * The operation is thread-safe.
     *
     * @param peer_address The address of the peer to add (e.g., "192.168.1.1:6868").
     */
    void add_peer(const std::string& peer_address);

    /**
     * @brief Removes a peer address from the manager.
     *
     * This method removes a specified peer address from the internal set of known peers.
     * The operation is thread-safe.
     *
     * @param peer_address The address of the peer to remove.
     */
    void remove_peer(const std::string& peer_address);

    /**
     * @brief Retrieves a list of all known peer addresses.
     *
     * This method returns a vector containing all the peer addresses currently
     * managed by the `PeerManager`. The operation is thread-safe.
     *
     * @return A `std::vector<std::string>` containing all known peer addresses.
     */
    std::vector<std::string> get_peers() const;

    /**
     * @brief Selects and returns a random peer address from the managed list.
     *
     * This method provides a way to randomly select a peer, which can be useful
     * for peer discovery or for initiating connections to a diverse set of nodes.
     * The operation is thread-safe. If no peers are available, it returns an empty string.
     *
     * @return A `std::string` representing a randomly selected peer address, or an empty string if no peers are available.
     */
    std::string get_random_peer() const;

private:
    std::set<std::string> peers_; ///< @var peers_ A thread-safe set storing unique peer addresses in "ip:port" format.
    mutable std::mutex mutex_; ///< @var mutex_ A mutex to protect `peers_` from concurrent access, ensuring thread safety.
    mutable std::default_random_engine random_engine_; ///< @var random_engine_ A random number generator used for selecting random peers.
};

} // namespace chrono_p2p