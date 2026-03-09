#include "fragmentation.hpp"
#include "util/log.hpp"

namespace chrono_p2p {

std::vector<MessageFragment> FragmentationManager::fragment_message(
    const std::string& message_id,
    const std::string& serialized_data,
    uint32_t original_type
) {
    std::vector<MessageFragment> fragments;
    size_t total_size = serialized_data.size();
    uint32_t total_fragments = (total_size + MAX_FRAGMENT_SIZE - 1) / MAX_FRAGMENT_SIZE;

    for (uint32_t i = 0; i < total_fragments; ++i) {
        size_t start = i * MAX_FRAGMENT_SIZE;
        size_t length = std::min(MAX_FRAGMENT_SIZE, total_size - start);

        MessageFragment fragment;
        fragment.set_message_id(message_id);
        fragment.set_fragment_index(i);
        fragment.set_total_fragments(total_fragments);
        fragment.set_data(serialized_data.substr(start, length));
        fragment.set_original_type(original_type);

        fragments.push_back(fragment);
    }

    return fragments;
}

std::optional<P2PMessage> FragmentationManager::handle_fragment(const MessageFragment& fragment) {
    std::string msg_id = fragment.message_id();

    // Check if we already have this message tracking
    auto it = incomplete_messages_.find(msg_id);
    if (it == incomplete_messages_.end()) {
        IncompleteMessage info;
        info.total_fragments = fragment.total_fragments();
        info.received_count = 0;
        info.original_type = fragment.original_type();
        info.last_update = std::chrono::steady_clock::now();
        incomplete_messages_[msg_id] = info;
        it = incomplete_messages_.find(msg_id);
    }

    IncompleteMessage& info = it->second;
    
    // Update timestamp
    info.last_update = std::chrono::steady_clock::now();

    // Check consistency
    if (info.total_fragments != fragment.total_fragments()) {
        LOG_WARN(chrono_util::LogCategory::P2P, "Fragment total count mismatch for msg {}", msg_id);
        return std::nullopt;
    }

    // Store fragment if not already present
    if (info.fragments.find(fragment.fragment_index()) == info.fragments.end()) {
        info.fragments[fragment.fragment_index()] = fragment.data();
        info.received_count++;
    }

    // Check if complete
    if (info.received_count == info.total_fragments) {
        // Reassemble
        std::string full_data;
        for (uint32_t i = 0; i < info.total_fragments; ++i) {
            full_data += info.fragments[i];
        }

        // Create P2PMessage
        P2PMessage msg;
        bool success = false;

        // We need to parse based on original_type
        // 3 = BlockMessage, 5 = TransactionMessage, 15 = SnapshotChunkMessage
        switch (info.original_type) {
            case 3: { // BlockMessage
                BlockMessage block_msg;
                if (block_msg.ParseFromString(full_data)) {
                    *msg.mutable_block() = block_msg;
                    success = true;
                }
                break;
            }
            case 5: { // TransactionMessage
                TransactionMessage tx_msg;
                if (tx_msg.ParseFromString(full_data)) {
                    *msg.mutable_transaction() = tx_msg;
                    success = true;
                }
                break;
            }
            case 15: { // SnapshotChunkMessage
                 SnapshotChunkMessage chunk_msg;
                 if (chunk_msg.ParseFromString(full_data)) {
                     *msg.mutable_snapshot_chunk() = chunk_msg;
                     success = true;
                 }
                 break;
            }
            default:
                LOG_WARN(chrono_util::LogCategory::P2P, "Unknown original type {} for fragmented message", info.original_type);
                break;
        }

        // Cleanup
        incomplete_messages_.erase(it);

        if (success) {
            return msg;
        } else {
            LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to parse reassembled message {}", msg_id);
            return std::nullopt;
        }
    }

    return std::nullopt;
}

void FragmentationManager::cleanup() {
    auto now = std::chrono::steady_clock::now();
    for (auto it = incomplete_messages_.begin(); it != incomplete_messages_.end(); ) {
        if (now - it->second.last_update > MESSAGE_TIMEOUT) {
            it = incomplete_messages_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace chrono_p2p
