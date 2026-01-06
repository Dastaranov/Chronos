#include "storage/snapshots.hpp"
#include "util/log.hpp"
#include <stdexcept>
#include <string>
#include <nlohmann/json.hpp> // For serializing state (temporarily)

namespace chrono_storage {

SnapshotManager::SnapshotManager(std::unique_ptr<IKv> snapshot_kv_store)
    : kv_store_(std::move(snapshot_kv_store)) {
    if (!kv_store_) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "SnapshotManager initialized with a null IKv store.");
        throw std::runtime_error("IKv store cannot be null.");
    }
    LOG_INFO(chrono_util::LogCategory::STORAGE, "SnapshotManager initialized.");
}

bool SnapshotManager::createSnapshot(uint64_t height, const chrono_ledger::State& state, const chrono_util::Bytes& last_block_hash) {
    try {
        // Create a key for the snapshot (e.g., "snapshot_HEIGHT")
        std::string snapshot_key_str = "snapshot_" + std::to_string(height);
        chrono_util::Bytes snapshot_key(snapshot_key_str.begin(), snapshot_key_str.end());

        // Serialize state using the new binary format
        chrono_util::Bytes state_bytes = const_cast<chrono_ledger::State&>(state).serialize_to_bytes();
        
        // Create metadata JSON with height and last_block_hash
        nlohmann::json metadata;
        metadata["height"] = height;
        metadata["last_block_hash"] = chrono_util::bytes_to_hex(last_block_hash);
        
        // Combine metadata + state in single snapshot
        // Format: metadata_json (null-terminated) + state_bytes
        chrono_util::Bytes metadata_str = chrono_util::string_to_bytes(metadata.dump());
        
        // Use separator: metadata + null byte + state_bytes
        chrono_util::Bytes combined_snapshot;
        combined_snapshot.insert(combined_snapshot.end(), metadata_str.begin(), metadata_str.end());
        combined_snapshot.push_back(0);  // Null separator
        combined_snapshot.insert(combined_snapshot.end(), state_bytes.begin(), state_bytes.end());
        
        if (kv_store_->put(snapshot_key, combined_snapshot)) {
            LOG_INFO(chrono_util::LogCategory::STORAGE, "Snapshot created successfully at height {} with {} bytes of state data", height, state_bytes.size());
            return true;
        } else {
            LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to save snapshot to KV store at height {}", height);
            return false;
        }

    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Error creating snapshot at height {}: {}", height, e.what());
        return false;
    }
}

std::optional<SnapshotData> SnapshotManager::restoreSnapshot(uint64_t height) {
    try {
        // Create a key for the snapshot (e.g., "snapshot_HEIGHT")
        std::string snapshot_key_str = "snapshot_" + std::to_string(height);
        chrono_util::Bytes snapshot_key(snapshot_key_str.begin(), snapshot_key_str.end());

        std::optional<chrono_util::Bytes> combined_snapshot = kv_store_->get(snapshot_key);
        if (!combined_snapshot) {
            LOG_INFO(chrono_util::LogCategory::STORAGE, "No snapshot found at height {}", height);
            return std::nullopt;
        }

        auto result = restoreFromBytes(*combined_snapshot);
        if (result) {
             LOG_INFO(chrono_util::LogCategory::STORAGE, "Snapshot restored successfully from height {} with {} bytes of state data", height, result->state_bytes.size());
        }
        return result;

    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Error restoring snapshot at height {}: {}", height, e.what());
        return std::nullopt;
    }
}

chrono_util::Bytes SnapshotManager::getSnapshotChunk(uint64_t height, uint64_t chunk_index, uint64_t chunk_size) {
    try {
        std::string snapshot_key_str = "snapshot_" + std::to_string(height);
        chrono_util::Bytes snapshot_key(snapshot_key_str.begin(), snapshot_key_str.end());

        std::optional<chrono_util::Bytes> combined_snapshot = kv_store_->get(snapshot_key);
        if (!combined_snapshot) {
            return {};
        }

        size_t start_pos = chunk_index * chunk_size;
        if (start_pos >= combined_snapshot->size()) {
            return {};
        }

        size_t end_pos = std::min(start_pos + chunk_size, combined_snapshot->size());
        return chrono_util::Bytes(combined_snapshot->begin() + start_pos, combined_snapshot->begin() + end_pos);

    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Error getting snapshot chunk at height {}: {}", height, e.what());
        return {};
    }
}

uint64_t SnapshotManager::getSnapshotSize(uint64_t height) {
    try {
        std::string snapshot_key_str = "snapshot_" + std::to_string(height);
        chrono_util::Bytes snapshot_key(snapshot_key_str.begin(), snapshot_key_str.end());

        std::optional<chrono_util::Bytes> combined_snapshot = kv_store_->get(snapshot_key);
        if (!combined_snapshot) {
            return 0;
        }
        return combined_snapshot->size();
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Error getting snapshot size at height {}: {}", height, e.what());
        return 0;
    }
}

std::optional<SnapshotData> SnapshotManager::restoreFromBytes(const chrono_util::Bytes& combined_snapshot) {
    try {
        // Find null separator between metadata and state
        size_t separator_pos = combined_snapshot.size();
        for (size_t i = 0; i < combined_snapshot.size(); ++i) {
            if (combined_snapshot[i] == 0) {
                separator_pos = i;
                break;
            }
        }

        if (separator_pos == combined_snapshot.size()) {
            LOG_ERROR(chrono_util::LogCategory::STORAGE, "Invalid snapshot format: no separator found");
            return std::nullopt;
        }

        // Parse metadata JSON
        std::string metadata_str(combined_snapshot.begin(), combined_snapshot.begin() + separator_pos);
        nlohmann::json metadata = nlohmann::json::parse(metadata_str);

        // Extract state bytes
        chrono_util::Bytes state_bytes(combined_snapshot.begin() + separator_pos + 1, combined_snapshot.end());

        SnapshotData restored_data;
        restored_data.height = metadata.at("height").get<uint64_t>();
        restored_data.last_block_hash = chrono_util::hex_to_bytes(metadata.at("last_block_hash").get<std::string>());
        restored_data.state_bytes = state_bytes;

        return restored_data;

    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Error restoring snapshot from bytes: {}", e.what());
        return std::nullopt;
    }
}

} // namespace chrono_storage

