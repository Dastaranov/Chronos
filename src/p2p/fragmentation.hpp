#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <chrono>
#include "proto/p2p_messages.pb.h"

namespace chrono_p2p {

class FragmentationManager {
public:
    struct IncompleteMessage {
        uint32_t total_fragments;
        uint32_t received_count;
        std::map<uint32_t, std::string> fragments; // index -> data
        uint32_t original_type;
        std::chrono::steady_clock::time_point last_update;
    };

    // Constants
    static constexpr size_t MAX_FRAGMENT_SIZE = 64 * 1024; // 64KB
    static constexpr auto MESSAGE_TIMEOUT = std::chrono::seconds(60);

    // Split a serialized message into fragments
    static std::vector<MessageFragment> fragment_message(
        const std::string& message_id,
        const std::string& serialized_data,
        uint32_t original_type
    );

    // Handle an incoming fragment. Returns the reassembled P2PMessage if complete.
    std::optional<P2PMessage> handle_fragment(const MessageFragment& fragment);

    // Cleanup old incomplete messages
    void cleanup();

private:
    std::map<std::string, IncompleteMessage> incomplete_messages_;
};

} // namespace chrono_p2p
