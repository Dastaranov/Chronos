#include "peer_store.hpp"
#include "util/log.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace chrono_p2p {

using json = nlohmann::json;

PeerStore::PeerStore(const std::string& db_path) : db_path_(db_path) {}

void PeerStore::add_peer(const PeerRecord& peer) {
    peers_[peer.node_id] = peer;
}

void PeerStore::update_last_seen(const std::string& node_id) {
    if (peers_.find(node_id) != peers_.end()) {
        peers_[node_id].last_seen = std::chrono::system_clock::now();
    }
}

void PeerStore::update_reputation(const std::string& node_id, int delta) {
    if (peers_.find(node_id) != peers_.end()) {
        peers_[node_id].reputation_score += delta;
    }
}

void PeerStore::set_validated(const std::string& node_id, bool validated) {
    if (peers_.find(node_id) != peers_.end()) {
        peers_[node_id].is_validated = validated;
    }
}

std::vector<PeerRecord> PeerStore::get_all_peers() const {
    std::vector<PeerRecord> result;
    for (const auto& pair : peers_) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<PeerRecord> PeerStore::get_recent_peers(int max_age_hours) const {
    std::vector<PeerRecord> result;
    auto now = std::chrono::system_clock::now();
    for (const auto& pair : peers_) {
        if (std::chrono::duration_cast<std::chrono::hours>(now - pair.second.last_seen).count() <= max_age_hours) {
            result.push_back(pair.second);
        }
    }
    return result;
}

std::optional<PeerRecord> PeerStore::get_peer(const std::string& node_id) const {
    auto it = peers_.find(node_id);
    if (it != peers_.end()) {
        return it->second;
    }
    return std::nullopt;
}

void PeerStore::remove_peer(const std::string& node_id) {
    peers_.erase(node_id);
}

void PeerStore::prune_old_peers(int max_age_days) {
    auto now = std::chrono::system_clock::now();
    for (auto it = peers_.begin(); it != peers_.end(); ) {
        if (std::chrono::duration_cast<std::chrono::hours>(now - it->second.last_seen).count() > max_age_days * 24) {
            it = peers_.erase(it);
        } else {
            ++it;
        }
    }
}

void PeerStore::save_to_disk() {
    json j_peers = json::array();
    for (const auto& pair : peers_) {
        const auto& p = pair.second;
        json j_peer;
        j_peer["node_id"] = p.node_id;
        j_peer["addresses"] = p.addresses;
        j_peer["last_seen"] = std::chrono::duration_cast<std::chrono::milliseconds>(p.last_seen.time_since_epoch()).count();
        j_peer["reputation_score"] = p.reputation_score;
        j_peer["is_validator"] = p.is_validator;
        j_peer["is_validated"] = p.is_validated;
        j_peers.push_back(j_peer);
    }

    std::ofstream file(db_path_);
    if (file.is_open()) {
        file << j_peers.dump(4);
    } else {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to save peer store to {}", db_path_);
    }
}

void PeerStore::load_from_disk() {
    std::ifstream file(db_path_);
    if (!file.is_open()) {
        LOG_INFO(chrono_util::LogCategory::P2P, "No existing peer store found at {}", db_path_);
        return;
    }

    try {
        json j_peers;
        file >> j_peers;
        
        peers_.clear();
        for (const auto& j_peer : j_peers) {
            PeerRecord p;
            p.node_id = j_peer["node_id"];
            p.addresses = j_peer["addresses"].get<std::vector<std::string>>();
            p.last_seen = std::chrono::system_clock::time_point(std::chrono::milliseconds(j_peer["last_seen"]));
            p.reputation_score = j_peer["reputation_score"];
            p.is_validator = j_peer["is_validator"];
            p.is_validated = j_peer.value("is_validated", false);
            peers_[p.node_id] = p;
        }
        LOG_INFO(chrono_util::LogCategory::P2P, "Loaded {} peers from disk", peers_.size());
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::P2P, "Failed to load peer store: {}", e.what());
    }
}

} // namespace chrono_p2p
