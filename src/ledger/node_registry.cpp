#include "ledger/node_registry.hpp"
#include "util/codec.hpp"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace chrono_ledger {

NodeRegistry::NodeRegistry() {}

chrono_util::Bytes NodeRegistry::serialize_to_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    chrono_util::Bytes result;

    // 1. Serialize Nodes
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(nodes_.size()), result);
    for (const auto& pair : nodes_) {
        const NodeRecord& record = pair.second;
        chrono_util::write_string_with_length(record.node_id, result);
        chrono_util::write_string_with_length(record.public_key, result);
        chrono_util::write_string_with_length(record.name, result);
        chrono_util::write_fixed_uint64_le(record.stake_nanos, result);
        chrono_util::write_fixed_uint64_le(record.last_seen, result);
        result.push_back(record.time_tier); // Serialize time_tier
        
        // Serialize double as string to avoid endianness/format issues
        std::stringstream ss;
        ss << std::setprecision(17) << record.uptime_score;
        chrono_util::write_string_with_length(ss.str(), result);

        // Pack flags into a single byte
        uint8_t flags = 0;
        if (record.is_suspended) flags |= 0x01;
        if (record.is_blacklisted) flags |= 0x02;
        if (record.is_approved) flags |= 0x04;
        result.push_back(flags);
    }

    // 2. Serialize Votes
    chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(votes_.size()), result);
    for (const auto& pair : votes_) {
        const std::string& candidate_id = pair.first;
        const std::set<std::string>& voters = pair.second;

        chrono_util::write_string_with_length(candidate_id, result);
        chrono_util::write_fixed_uint32_le(static_cast<uint32_t>(voters.size()), result);
        for (const auto& voter : voters) {
            chrono_util::write_string_with_length(voter, result);
        }
    }

    return result;
}

bool NodeRegistry::deserialize_from_bytes(const chrono_util::Bytes& data, size_t& offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.clear();
    votes_.clear();

    try {
        // 1. Deserialize Nodes
        uint32_t node_count = chrono_util::read_fixed_uint32_le(data, offset);
        for (uint32_t i = 0; i < node_count; ++i) {
            NodeRecord record;
            record.node_id = chrono_util::read_string_with_length(data, offset);
            record.public_key = chrono_util::read_string_with_length(data, offset);
            record.name = chrono_util::read_string_with_length(data, offset);
            record.stake_nanos = chrono_util::read_fixed_uint64_le(data, offset);
            record.last_seen = chrono_util::read_fixed_uint64_le(data, offset);
            
            if (offset >= data.size()) return false;
            record.time_tier = data[offset++]; // Deserialize time_tier
            
            std::string uptime_str = chrono_util::read_string_with_length(data, offset);
            try {
                record.uptime_score = std::stod(uptime_str);
            } catch (...) {
                record.uptime_score = 1.0; // Fallback
            }

            if (offset >= data.size()) return false;
            uint8_t flags = data[offset++];
            record.is_suspended = (flags & 0x01) != 0;
            record.is_blacklisted = (flags & 0x02) != 0;
            record.is_approved = (flags & 0x04) != 0;

            nodes_[record.node_id] = record;
        }

        // 2. Deserialize Votes
        uint32_t vote_entries = chrono_util::read_fixed_uint32_le(data, offset);
        for (uint32_t i = 0; i < vote_entries; ++i) {
            std::string candidate_id = chrono_util::read_string_with_length(data, offset);
            uint32_t voter_count = chrono_util::read_fixed_uint32_le(data, offset);
            
            std::set<std::string> voters;
            for (uint32_t j = 0; j < voter_count; ++j) {
                voters.insert(chrono_util::read_string_with_length(data, offset));
            }
            votes_[candidate_id] = voters;
        }

        return true;
    } catch (const std::exception& e) {
        LOG_ERROR(chrono_util::LogCategory::STORAGE, "Failed to deserialize NodeRegistry: {}", e.what());
        return false;
    }
}

bool NodeRegistry::register_node(const std::string& node_id, const std::string& public_key, const std::string& name, uint64_t initial_stake, uint8_t time_tier) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (nodes_.find(node_id) != nodes_.end()) {
        LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Node {} already registered", node_id);
        return false;
    }

    NodeRecord record;
    record.node_id = node_id;
    record.public_key = public_key;
    record.name = name;
    record.stake_nanos = initial_stake;
    record.last_seen = 0;
    record.uptime_score = 1.0; // Start with perfect score
    record.time_tier = time_tier;
    record.is_suspended = false;
    record.is_blacklisted = false;
    record.is_approved = false; // Requires voting to become active

    nodes_[node_id] = record;
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Registered node: {} ({}) with stake {} and tier {}", name, node_id, initial_stake, time_tier);
    return true;
}

bool NodeRegistry::unregister_node(const std::string& node_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) {
        return false;
    }
    nodes_.erase(it);
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Unregistered node: {}", node_id);
    return true;
}

void NodeRegistry::update_last_seen(const std::string& node_id, uint64_t timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.last_seen = timestamp;
    }
}

void NodeRegistry::update_stake(const std::string& node_id, int64_t delta_nanos) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return;

    if (delta_nanos > 0) {
        it->second.stake_nanos += static_cast<uint64_t>(delta_nanos);
    } else {
        uint64_t deduction = static_cast<uint64_t>(-delta_nanos);
        if (it->second.stake_nanos >= deduction) {
            it->second.stake_nanos -= deduction;
        } else {
            it->second.stake_nanos = 0;
            it->second.is_suspended = true; // Auto-suspend if stake depleted
            LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Node {} stake depleted. Suspended.", node_id);
        }
    }
}

void NodeRegistry::slash_node(const std::string& node_id, uint64_t penalty_nanos, const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return;

    LOG_WARN(chrono_util::LogCategory::CONSENSUS, "Slashing node {} for {}: {} nanos", node_id, reason, penalty_nanos);
    
    if (it->second.stake_nanos >= penalty_nanos) {
        it->second.stake_nanos -= penalty_nanos;
    } else {
        it->second.stake_nanos = 0;
        it->second.is_suspended = true;
    }
    
    // Reduce uptime score as penalty
    it->second.uptime_score *= 0.9; 
}

std::optional<NodeRecord> NodeRegistry::get_node(const std::string& node_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<NodeRecord> NodeRegistry::get_all_nodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeRecord> result;
    result.reserve(nodes_.size());
    for (const auto& pair : nodes_) {
        result.push_back(pair.second);
    }
    return result;
}

std::vector<NodeRecord> NodeRegistry::get_active_validators(uint64_t min_stake) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<NodeRecord> result;
    for (const auto& pair : nodes_) {
        const auto& node = pair.second;
        if (!node.is_suspended && !node.is_blacklisted && node.is_approved && node.stake_nanos >= min_stake) {
            result.push_back(node);
        }
    }
    return result;
}

std::vector<NodeRecord> NodeRegistry::get_top_validators(size_t n) const {
    auto active = get_active_validators(0); // Get all non-suspended
    
    // Sort by stake descending
    std::sort(active.begin(), active.end(), [](const NodeRecord& a, const NodeRecord& b) {
        return a.stake_nanos > b.stake_nanos;
    });

    if (active.size() > n) {
        active.resize(n);
    }
    return active;
}

bool NodeRegistry::is_eligible_validator(const std::string& node_id, uint64_t min_stake) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it == nodes_.end()) return false;
    
    const auto& node = it->second;
    
    uint64_t required_stake = min_stake;
    switch (node.time_tier) {
        case 1: required_stake *= 100; break;
        case 2: required_stake *= 10; break;
        case 3: required_stake *= 5; break;
        case 4: required_stake *= 2; break;
        default: break; // Tier 5 uses base stake
    }

    return !node.is_suspended && !node.is_blacklisted && node.is_approved && node.stake_nanos >= required_stake;
}

void NodeRegistry::set_suspension(const std::string& node_id, bool suspended) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(node_id);
    if (it != nodes_.end()) {
        it->second.is_suspended = suspended;
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Node {} suspension set to {}", node_id, suspended);
    }
}

bool NodeRegistry::vote_for_node(const std::string& voter_id, const std::string& candidate_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Check if candidate exists
    if (nodes_.find(candidate_id) == nodes_.end()) {
        return false;
    }

    // Check if voter is a valid validator (must have stake)
    auto voter_it = nodes_.find(voter_id);
    if (voter_it == nodes_.end() || voter_it->second.stake_nanos == 0) {
        return false;
    }

    votes_[candidate_id].insert(voter_id);
    LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Node {} voted for {}", voter_id, candidate_id);
    return true;
}

bool NodeRegistry::check_approval(const std::string& candidate_id, size_t total_validators, double threshold_ratio) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = nodes_.find(candidate_id);
    if (it == nodes_.end()) return false;

    if (it->second.is_approved) return true; // Already approved

    size_t vote_count = votes_[candidate_id].size();
    size_t required = static_cast<size_t>(total_validators * threshold_ratio);
    
    if (vote_count >= required && required > 0) {
        it->second.is_approved = true;
        LOG_INFO(chrono_util::LogCategory::CONSENSUS, "Node {} approved with {}/{} votes", candidate_id, vote_count, total_validators);
        return true;
    }
    return false;
}

size_t NodeRegistry::get_pending_approval_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = 0;
    for (const auto& pair : nodes_) {
        if (!pair.second.is_approved) {
            count++;
        }
    }
    return count;
}

std::vector<CandidateDetails> NodeRegistry::get_candidates() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<CandidateDetails> candidates;
    
    // Calculate active validators count internally
    size_t total_validators = 0;
    for (const auto& pair : nodes_) {
        const auto& node = pair.second;
        if (!node.is_suspended && !node.is_blacklisted && node.is_approved && node.stake_nanos > 0) {
            total_validators++;
        }
    }
    
    // Default threshold 0.66
    size_t required = static_cast<size_t>(total_validators * 0.66);
    if (required == 0 && total_validators > 0) required = 1;

    for (const auto& pair : nodes_) {
        if (!pair.second.is_approved) {
            CandidateDetails details;
            details.record = pair.second;
            
            auto vote_it = votes_.find(pair.first);
            details.votes_for = (vote_it != votes_.end()) ? vote_it->second.size() : 0;
            
            if (total_validators >= details.votes_for) {
                details.votes_against = total_validators - details.votes_for;
            } else {
                details.votes_against = 0;
            }
            
            details.votes_required = required;
            candidates.push_back(details);
        }
    }
    return candidates;
}

nlohmann::json NodeRegistry::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    nlohmann::json j_nodes = nlohmann::json::array();
    for (const auto& pair : nodes_) {
        j_nodes.push_back(pair.second.to_json());
    }
    
    nlohmann::json j_votes = nlohmann::json::object();
    for (const auto& pair : votes_) {
        j_votes[pair.first] = pair.second;
    }

    return {
        {"nodes", j_nodes},
        {"votes", j_votes}
    };
}

void NodeRegistry::from_json(const nlohmann::json& j) {
    std::lock_guard<std::mutex> lock(mutex_);
    nodes_.clear();
    votes_.clear();

    if (j.is_array()) {
        // Legacy format
        for (const auto& item : j) {
            NodeRecord record = NodeRecord::from_json(item);
            nodes_[record.node_id] = record;
        }
    } else if (j.is_object()) {
        // New format
        if (j.contains("nodes")) {
            for (const auto& item : j["nodes"]) {
                NodeRecord record = NodeRecord::from_json(item);
                nodes_[record.node_id] = record;
            }
        }
        if (j.contains("votes")) {
            for (const auto& item : j["votes"].items()) {
                votes_[item.key()] = item.value().get<std::set<std::string>>();
            }
        }
    }
}

} // namespace chrono_ledger
