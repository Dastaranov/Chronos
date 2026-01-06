#pragma once

#include "storage/IKv.hpp"
#include <string>
#include <fstream>
#include <mutex>
#include <vector>
#include <unordered_map>
#include "util/bytes.hpp"

namespace chrono_storage {

/**
 * @class FileKv
 * @brief A simple file-based implementation of the IKv key-value storage interface.
 *
 * This class provides a persistent key-value store using a single file.
 * It uses an in-memory cache to reduce disk I/O and supports append-only writes
 * with periodic compaction.
 */
class FileKv : public IKv {
public:
    /**
     * @brief Constructs a FileKv instance.
     * @param file_path The path to the file used for storage.
     */
    explicit FileKv(const std::string& file_path);

    /**
     * @brief Destructor.
     */
    ~FileKv() override;

    /**
     * @brief Retrieves the value for a given key.
     * @param key The key to look up.
     * @return An optional containing the value if found, otherwise std::nullopt.
     */
    std::optional<chrono_util::Bytes> get(const chrono_util::Bytes& key) const override;

    /**
     * @brief Stores a key-value pair.
     * @param key The key.
     * @param value The value.
     */
    bool put(const chrono_util::Bytes& key, const chrono_util::Bytes& value) override;

    /**
     * @brief Removes a key-value pair.
     * @param key The key to remove.
     */
    void remove(const chrono_util::Bytes& key) override;

private:
    /// The path to the storage file.
    std::string file_path_;
    /// Mutex for thread-safe file access.
    mutable std::mutex mutex_;
    /// In-memory cache of key-value pairs.
    std::unordered_map<chrono_util::Bytes, chrono_util::Bytes, chrono_util::BytesHasher> cache_;
    /// Counter for append operations since last compaction.
    size_t append_count_ = 0;
    /// Threshold for triggering compaction (rewrite).
    static const size_t COMPACTION_THRESHOLD = 1000;

    /**
     * @brief Reads all key-value pairs from the file.
     * @return A vector of key-value pairs.
     */
    std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>> read_all() const;

    /**
     * @brief Rewrites the entire file with the given data.
     * @param data The vector of key-value pairs to write.
     */
    void rewrite_file(const std::vector<std::pair<chrono_util::Bytes, chrono_util::Bytes>>& data);
    
    /**
     * @brief Appends a key-value pair to the file.
     * @param key The key.
     * @param value The value.
     */
    void append_to_file(const chrono_util::Bytes& key, const chrono_util::Bytes& value);
};

} // namespace chrono_storage