//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file peer_manager.cpp
 * @brief This file implements the PeerManager class, responsible for managing a node's connections to other peers.
 *
 * The PeerManager class provides thread-safe operations for adding, removing, and retrieving
 * peer information. It uses a `std::set` to store unique peer addresses and a `std::mutex`
 * to ensure thread safety for concurrent access. Random peer selection is also supported.
 *
 * Key functions implemented:
 * - `PeerManager::PeerManager()`: Constructor to initialize the peer manager.
 * - `PeerManager::add_peer()`: Adds a new peer to the manager's list.
 * - `PeerManager::remove_peer()`: Removes a peer from the list.
 * - `PeerManager::get_peers()`: Retrieves a list of all known peers.
 * - `PeerManager::get_random_peer()`: Selects a random peer from the list.
 */

#include "p2p/peer_manager.hpp"
#include <algorithm> // For std::shuffle
#include <random>    // For std::default_random_engine, std::uniform_int_distribution
#include <chrono>    // For std::chrono::system_clock

namespace chrono_p2p {

/**
 * @brief Constructs a PeerManager object.
 *
 * Initializes the random number engine using the current system time to ensure
 * a different sequence of random numbers each time the application runs.
 */
PeerManager::PeerManager()
    : random_engine_(std::chrono::system_clock::now().time_since_epoch().count()) {}

/**
 * @brief Adds a new peer address to the manager.
 *
 * This method adds a peer's address (e.g., "ip:port") to the internal `peers_` set.
 * A `std::lock_guard` is used to ensure thread-safe access to the `peers_` set.
 * If the peer is new, an informational message is logged.
 *
 * @param peer_address The address of the peer to add.
 */
void PeerManager::add_peer(const std::string& peer_address) {
    std::lock_guard<std::mutex> lock(mutex_); ///< @var lock A lock guard to ensure exclusive access to `peers_`.
    if (peers_.insert(peer_address).second) {
        LOG_INFO(chrono_util::LogCategory::P2P, "Added new peer: {}", peer_address);
    }
}

/**
 * @brief Removes a peer address from the manager.
 *
 * This method removes a specified peer address from the internal `peers_` set.
 * A `std::lock_guard` is used to ensure thread-safe access. If the peer was
 * successfully removed, an informational message is logged.
 *
 * @param peer_address The address of the peer to remove.
 */
void PeerManager::remove_peer(const std::string& peer_address) {
    std::lock_guard<std::mutex> lock(mutex_); ///< @var lock A lock guard to ensure exclusive access to `peers_`.
    if (peers_.erase(peer_address) > 0) {
        LOG_INFO(chrono_util::LogCategory::P2P, "Removed peer: {}", peer_address);
    }
}

/**
 * @brief Retrieves a list of all known peer addresses.
 *
 * This method returns a `std::vector` containing all the peer addresses currently
 * managed by the `PeerManager`. A `std::lock_guard` ensures thread-safe access
 * to the `peers_` set during the copy operation.
 *
 * @return A `std::vector<std::string>` containing all known peer addresses.
 */
std::vector<std::string> PeerManager::get_peers() const {
    std::lock_guard<std::mutex> lock(mutex_); ///< @var lock A lock guard to ensure exclusive access to `peers_`.
    return std::vector<std::string>(peers_.begin(), peers_.end());
}

/**
 * @brief Selects and returns a random peer address from the managed list.
 *
 * This method provides a way to randomly select a peer. It first copies the `peers_`
 * set into a vector, shuffles the vector using `std::shuffle` and the internal
 * random engine, and then returns the first element. A `std::lock_guard` ensures
 * thread-safe access to `peers_`. If no peers are available, it returns an empty string.
 *
 * @return A `std::string` representing a randomly selected peer address, or an empty string if no peers are available.
 */
std::string PeerManager::get_random_peer() const {
    std::lock_guard<std::mutex> lock(mutex_); ///< @var lock A lock guard to ensure exclusive access to `peers_`.
    if (peers_.empty()) {
        return "";
    }
    std::vector<std::string> shuffled_peers(peers_.begin(), peers_.end()); ///< @var shuffled_peers A temporary vector holding peer addresses for shuffling.
    std::shuffle(shuffled_peers.begin(), shuffled_peers.end(), random_engine_);
    return shuffled_peers[0];
}

} // namespace chrono_p2p