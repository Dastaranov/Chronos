#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <set>
#include <optional>
#include <mutex>
#include <nlohmann/json.hpp>
#include "util/bytes.hpp"
#include "util/log.hpp"

namespace chrono_ledger {

struct NodeRecord {
    std::string node_id;       // Address (Base58Check)
    std::string public_key;    // Hex encoded public key
    std::string name;          // Human readable name
    uint64_t stake_nanos;      // Amount staked
    uint64_t last_seen;        // Timestamp of last activity
    double uptime_score;       // 0.0 to 1.0
    uint8_t time_tier;         // 1=Quantum, 2=Atomic, 3=GPS, 4=NTS, 5=NTP
    bool is_suspended;         // If true, cannot validate
    bool is_blacklisted;       // If true, banned permanently
    bool is_approved;          // If true, approved by governance

    nlohmann::json to_json() const {
        return {
            {"node_id", node_id},
            {"public_key", public_key},
            {"name", name},
            {"stake_nanos", stake_nanos},
            {"last_seen", last_seen},
            {"uptime_score", uptime_score},
            {"time_tier", time_tier},
            {"is_suspended", is_suspended},
            {"is_blacklisted", is_blacklisted},
            {"is_approved", is_approved}
        };
    }

    static NodeRecord from_json(const nlohmann::json& j) {
        NodeRecord record;
        record.node_id = j.at("node_id").get<std::string>();
        record.public_key = j.value("public_key", ""); // Default empty for backward compatibility
        record.name = j.at("name").get<std::string>();
        record.stake_nanos = j.at("stake_nanos").get<uint64_t>();
        record.last_seen = j.at("last_seen").get<uint64_t>();
        record.uptime_score = j.at("uptime_score").get<double>();
        record.time_tier = j.value("time_tier", (uint8_t)5); // Default to NTP
        record.is_suspended = j.at("is_suspended").get<bool>();
        record.is_blacklisted = j.at("is_blacklisted").get<bool>();
        record.is_approved = j.value("is_approved", true);
        return record;
    }
};

struct CandidateDetails {
    NodeRecord record;
    size_t votes_for;
    size_t votes_against;
    size_t votes_required;
    
    nlohmann::json to_json() const {
        nlohmann::json j = record.to_json();
        j["votes_for"] = votes_for;
        j["votes_against"] = votes_against;
        j["votes_required"] = votes_required;
        return j;
    }
};

class NodeRegistry {
public:
    NodeRegistry();
    ~NodeRegistry() = default;

    // Serialization
    nlohmann::json to_json() const;
    void from_json(const nlohmann::json& j);
    
    // Binary Serialization
    chrono_util::Bytes serialize_to_bytes() const;
    bool deserialize_from_bytes(const chrono_util::Bytes& data, size_t& offset);

    // Registration
    bool register_node(const std::string& node_id, const std::string& public_key, const std::string& name, uint64_t initial_stake, uint8_t time_tier = 5);
    bool unregister_node(const std::string& node_id);

    // Updates
    void update_last_seen(const std::string& node_id, uint64_t timestamp);
    void update_stake(const std::string& node_id, int64_t delta_nanos);
    void slash_node(const std::string& node_id, uint64_t penalty_nanos, const std::string& reason);
    
    // Queries
    std::optional<NodeRecord> get_node(const std::string& node_id) const;
    std::vector<NodeRecord> get_all_nodes() const;
    std::vector<NodeRecord> get_active_validators(uint64_t min_stake) const;
    std::vector<NodeRecord> get_top_validators(size_t n) const;

    // Governance
    bool is_eligible_validator(const std::string& node_id, uint64_t min_stake) const;
    void set_suspension(const std::string& node_id, bool suspended);
    
    // Voting
    bool vote_for_node(const std::string& voter_id, const std::string& candidate_id);
    bool check_approval(const std::string& candidate_id, size_t total_validators, double threshold_ratio = 0.66);
    size_t get_pending_approval_count() const;
    std::vector<CandidateDetails> get_candidates() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, NodeRecord> nodes_;
    std::unordered_map<std::string, std::set<std::string>> votes_; // candidate_id -> set of voter_ids
};

} // namespace chrono_ledger
