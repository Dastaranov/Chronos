#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>

namespace chrono_p2p {

struct PeerRecord {
    std::string node_id;
    std::vector<std::string> addresses;  // Can have multiple IPs
    std::chrono::system_clock::time_point last_seen;
    int reputation_score = 0;
    bool is_validator = false;
    bool is_validated = false; // New: Main Node Validation status
};

class PeerStore {
public:
    PeerStore(const std::string& db_path);
    
    void add_peer(const PeerRecord& peer);
    void update_last_seen(const std::string& node_id);
    void update_reputation(const std::string& node_id, int delta);
    void set_validated(const std::string& node_id, bool validated);
    
    std::vector<PeerRecord> get_all_peers() const;
    std::vector<PeerRecord> get_recent_peers(int max_age_hours = 24) const;
    std::optional<PeerRecord> get_peer(const std::string& node_id) const;
    
    void remove_peer(const std::string& node_id);
    void prune_old_peers(int max_age_days = 30);
    
    void save_to_disk();
    void load_from_disk();
    
private:
    std::string db_path_;
    std::unordered_map<std::string, PeerRecord> peers_;
};

} // namespace chrono_p2p
