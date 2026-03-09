//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file snapshots.hpp
 * @brief Defines the SnapshotManager class for creating and managing blockchain state snapshots.
 */

#pragma once

#include "ledger/state.hpp"
#include "storage/IKv.hpp"
#include "util/bytes.hpp"
#include <memory>
#include <string>
#include <optional> // NEW: For std::optional

namespace chrono_storage {

struct SnapshotData {
    uint64_t height;
    chrono_util::Bytes last_block_hash;
    chrono_util::Bytes state_bytes;  // Binary serialized state data
};

/**
 * @class SnapshotManager
 * @brief Manages the creation and restoration of blockchain state snapshots.
 *
 * This class provides functionality to capture the current state of the ledger
 * and other critical blockchain data at specific block heights, allowing for
 * faster synchronization of new nodes or recovery.
 */
class SnapshotManager {
public:
    /**
     * @brief Constructs a SnapshotManager instance.
     * @param snapshot_kv_store A unique pointer to an IKv implementation for storing snapshot data.
     */
    explicit SnapshotManager(std::unique_ptr<IKv> snapshot_kv_store);

    /**
     * @brief Creates a new state snapshot at the specified block height.
     *
     * This method captures the current ledger state and associated metadata, serializes it,
     * and stores it persistently.
     *
     * @param height The block height at which the snapshot is being taken.
     * @param state The current ledger state to be snapshotted.
     * @param last_block_hash The hash of the last block included in the snapshot.
     * @return `true` if the snapshot was created successfully, `false` otherwise.
     */
    bool createSnapshot(uint64_t height, const chrono_ledger::State& state, const chrono_util::Bytes& last_block_hash);

    /**
     * @brief Restores the node's state from a snapshot at a specified block height.
     *
     * This method loads a previously saved snapshot and reconstructs the ledger state
     * and other relevant metadata.
     *
     * @param height The block height of the snapshot to restore.
     * @return An optional `SnapshotData` object if the snapshot is found and successfully restored,
     *         otherwise `std::nullopt`.
     */
    std::optional<SnapshotData> restoreSnapshot(uint64_t height);

    /**
     * @brief Retrieves a specific chunk of a snapshot.
     * 
     * @param height The snapshot height.
     * @param chunk_index The index of the chunk (0-based).
     * @param chunk_size The size of chunks in bytes.
     * @return The chunk data bytes, or empty if not found/out of bounds.
     */
    chrono_util::Bytes getSnapshotChunk(uint64_t height, uint64_t chunk_index, uint64_t chunk_size);

    /**
     * @brief Gets the total size of a snapshot in bytes.
     * @param height The snapshot height.
     * @return The size in bytes, or 0 if not found.
     */
    uint64_t getSnapshotSize(uint64_t height);

    /**
     * @brief Restores state from raw snapshot bytes (reassembled from chunks).
     * @param data The complete raw snapshot data.
     * @return An optional `SnapshotData` object if successful.
     */
    std::optional<SnapshotData> restoreFromBytes(const chrono_util::Bytes& data);

private:
    std::unique_ptr<IKv> kv_store_; ///< @var kv_store_ The underlying key-value store for snapshot persistence.
};

} // namespace chrono_storage
