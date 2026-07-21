//
// Created by Chronos | 2025 | Belgium
//

/**
 * @file config.cpp
 * @brief This file implements the Config class for loading Chronos node configuration from TOML files.
 *
 * This implementation provides the logic to parse a TOML configuration file and populate
 * a `Config` object with settings such as data directory, RPC port, P2P port, and seed peers.
 * It handles potential parsing errors and provides default values for configuration parameters.
 *
 * Key functions implemented:
 * - `Config::load(const std::string& file_path)`: Static method to load configuration from a TOML file.
 */

#include "node/config.hpp"
#include "toml.hpp"
#include "util/log.hpp"
#include <stdexcept>
#include <algorithm>

namespace chrono_node {

Config Config::load(const std::string& file_path) {
    Config cfg; ///< @var cfg A `Config` object to be populated with loaded settings.
    toml::table tbl;
    try {
        // Load default config first
        toml::table default_tbl = toml::parse_file("config/default.toml");
        tbl = default_tbl; // Start with defaults

        // Load user-specified config and merge, overriding defaults
        if (!file_path.empty()) {
            toml::table user_tbl = toml::parse_file(file_path);
            for (auto&& [key, val] : user_tbl) {
                tbl.insert_or_assign(key, val);
            }
        }

    } catch (const toml::parse_error& err) {
        throw std::runtime_error("Failed to parse config file: " + std::string(err.description()));
    } catch (const std::exception& e) {
        // Catch other potential errors during file operations or merging
        throw std::runtime_error("Error loading config: " + std::string(e.what()));
    }

    // --- Core settings ---
    cfg.data_dir = tbl["data_dir"].value_or(cfg.data_dir);
    
    // Load Storage settings
    if (auto storage_tbl = tbl["storage"].as_table()) {
        cfg.storage_backend = (*storage_tbl)["backend"].value_or(cfg.storage_backend);
        cfg.leveldb_path = (*storage_tbl)["leveldb_path"].value_or(cfg.leveldb_path);
    } else {
        // Fallback to core settings if storage section missing
        // If user set data_dir but not storage backend, we default to disk
    }
    
    // Load RPC port from its own section
    if (auto rpc_tbl = tbl["rpc"].as_table()) {
        cfg.rpc_port = (*rpc_tbl)["port"].value_or(cfg.rpc_port);
        cfg.rpc_bind_ip = (*rpc_tbl)["bind_ip"].value_or(cfg.rpc_bind_ip);
        cfg.rpc_api_key = (*rpc_tbl)["api_key"].value_or(cfg.rpc_api_key);
    }


    // --- Node Type setting ---
    // Check for node_type under [node] table
    if (auto node_tbl = tbl["node"].as_table()) {
        if (auto node_type_str = (*node_tbl)["node_type"].value<std::string>()) {
            if (*node_type_str == "light") {
                cfg.node_type = NodeType::LIGHT;
            } else if (*node_type_str == "full") {
                cfg.node_type = NodeType::FULL;
            }
        }
        cfg.is_beacon_node = (*node_tbl)["is_beacon_node"].value_or(cfg.is_beacon_node);
    }

    // --- Network settings ---
    if (auto network_tbl = tbl["network"].as_table()) {
        cfg.listen_addr = (*network_tbl)["listen_addr"].value_or(cfg.listen_addr);
        cfg.listen_port = (*network_tbl)["listen_port"].value_or(cfg.listen_port);
        if (auto network_seeds_arr = (*network_tbl)["seeds"].as_array()) {
            cfg.network_seeds.clear();
            for (const auto& item : *network_seeds_arr) {
                if (auto s = item.as<std::string>()) {
                    cfg.network_seeds.push_back(s->get());
                }
            }
        }
    }

    // --- P2P settings ---
    if (auto p2p_tbl = tbl["p2p"].as_table()) {
        cfg.enable_peer_discovery = (*p2p_tbl)["enable_peer_discovery"].value_or(cfg.enable_peer_discovery);
        cfg.max_peers = (*p2p_tbl)["max_peers"].value_or(cfg.max_peers);
        cfg.min_peers = (*p2p_tbl)["min_peers"].value_or(cfg.min_peers);
        cfg.peer_discovery_interval_ms = (*p2p_tbl)["peer_discovery_interval_ms"].value_or(cfg.peer_discovery_interval_ms);

        if (auto bootstrap_nodes_arr = (*p2p_tbl)["bootstrap_nodes"].as_array()) {
            cfg.bootstrap_nodes.clear();
            for (const auto& item : *bootstrap_nodes_arr) {
                if (auto s = item.as<std::string>()) {
                    cfg.bootstrap_nodes.push_back(s->get());
                }
            }
        }

        if (auto gossip_topics_arr = (*p2p_tbl)["gossip_topics"].as_array()) {
            cfg.gossip_topics.clear();
            for (const auto& item : *gossip_topics_arr) {
                if (auto s = item.as<std::string>()) {
                    cfg.gossip_topics.push_back(s->get());
                }
            }
        }
    }

    bool has_explicit_pot_epsilon = false;

    // --- Consensus settings ---
    if (auto consensus_tbl = tbl["consensus"].as_table()) {
        cfg.bft_round_timeout_ms = (*consensus_tbl)["bft_round_timeout_ms"].value_or(cfg.bft_round_timeout_ms);
        cfg.slot_ms = (*consensus_tbl)["slot_ms"].value_or(cfg.slot_ms);
        cfg.bft_quorum = (*consensus_tbl)["bft_quorum"].value_or(cfg.bft_quorum);
        cfg.outlier_mad_factor = (*consensus_tbl)["outlier_mad_factor"].value_or(cfg.outlier_mad_factor);
        cfg.min_threshold_ms = (*consensus_tbl)["min_threshold_ms"].value_or(cfg.min_threshold_ms);
        has_explicit_pot_epsilon = consensus_tbl->contains("pot_epsilon_ms");
        cfg.pot_epsilon_ms = (*consensus_tbl)["pot_epsilon_ms"].value_or(cfg.pot_epsilon_ms);
        cfg.pot_delta_min_ms = (*consensus_tbl)["pot_delta_min_ms"].value_or(cfg.pot_delta_min_ms);
        if (consensus_tbl->contains("min_stake_nanos")) {
            cfg.min_stake_nanos = (*consensus_tbl)["min_stake_nanos"].value_or(1000000ULL);
        }
        
        // Load validators
        if (auto validators_arr = (*consensus_tbl)["validators"].as_array()) {
            cfg.validators.clear();
            for (const auto& item : *validators_arr) {
                if (auto v = item.as<std::string>()) {
                    cfg.validators.push_back(v->get());
                }
            }
        }
    }

    // --- External Time Source settings ---
    if (auto ets_tbl = tbl["external_time_sources"].as_table()) {
        if (auto ntp_servers_arr = (*ets_tbl)["ntp_servers"].as_array()) {
            cfg.ntp_servers.clear();
            for (const auto& item : *ntp_servers_arr) {
                if (auto s = item.as<std::string>()) {
                    cfg.ntp_servers.push_back(s->get());
                }
            }
        }
        cfg.ntp_query_interval_ms = (*ets_tbl)["ntp_query_interval_ms"].value_or(cfg.ntp_query_interval_ms);
        cfg.time_backend = (*ets_tbl)["backend"].value_or(cfg.time_backend);
        cfg.atomic_clock_device = (*ets_tbl)["atomic_clock_device"].value_or(cfg.atomic_clock_device);
        cfg.quantum_clock_device = (*ets_tbl)["quantum_clock_device"].value_or(cfg.quantum_clock_device);
    }

    // --- Secure Time settings ---
    if (auto secure_time_tbl = tbl["secure_time"].as_table()) {
        // Use secure_time.max_skew_ms as default epsilon unless explicitly overridden in [consensus].
        // This keeps the PoT inequality parameter aligned with secure clock uncertainty settings.
        if (!has_explicit_pot_epsilon) {
            cfg.pot_epsilon_ms = (*secure_time_tbl)["max_skew_ms"].value_or(cfg.pot_epsilon_ms);
        }
    }

    // --- Crypto settings ---
    if (auto crypto_tbl = tbl["crypto"].as_table()) {
        cfg.sign_alg = (*crypto_tbl)["sign_alg"].value_or(cfg.sign_alg);
        cfg.prehash = (*crypto_tbl)["prehash"].value_or(cfg.prehash);
        cfg.addr_hrp = (*crypto_tbl)["addr_hrp"].value_or(cfg.addr_hrp);
        cfg.private_key = (*crypto_tbl)["private_key"].value_or(cfg.private_key);
        cfg.public_key = (*crypto_tbl)["public_key"].value_or(cfg.public_key);
        cfg.private_key_id = (*crypto_tbl)["private_key_id"].value_or(cfg.private_key_id);
    }

    // --- Tokenomics settings ---
    if (auto tokenomics_tbl = tbl["tokenomics"].as_table()) {
        cfg.max_total_supply = (*tokenomics_tbl)["max_total_supply"].value_or(cfg.max_total_supply);
        cfg.initial_block_reward_nanos = (*tokenomics_tbl)["initial_block_reward_nanos"].value_or(cfg.initial_block_reward_nanos);
        cfg.minting_enabled = (*tokenomics_tbl)["minting_enabled"].value_or(cfg.minting_enabled);
        cfg.reward_halving_interval = (*tokenomics_tbl)["reward_halving_interval"].value_or(cfg.reward_halving_interval);
        cfg.fee_burn_percentage = (*tokenomics_tbl)["fee_burn_percentage"].value_or(cfg.fee_burn_percentage);
        cfg.token_decimals = (*tokenomics_tbl)["token_decimals"].value_or(cfg.token_decimals);
        cfg.base_fee_nanos = (*tokenomics_tbl)["base_fee_nanos"].value_or(cfg.base_fee_nanos);
    }

    // --- Genesis settings ---
    if (auto genesis_tbl = tbl["genesis"].as_table()) {
        cfg.genesis_consensus_time = (*genesis_tbl)["consensus_time"].value_or(cfg.genesis_consensus_time);
        cfg.genesis_expected_hash = (*genesis_tbl)["expected_hash"].value_or(cfg.genesis_expected_hash);
        cfg.max_supply_per_account = (*genesis_tbl)["max_supply_per_account"].value_or(cfg.max_supply_per_account);
        
        // Load genesis allocations (address -> balance mapping)
        if (auto allocations_tbl = (*genesis_tbl)["allocations"].as_table()) {
            for (auto&& [key, val] : *allocations_tbl) {
                std::string address = std::string(key.str());
                if (auto balance = val.value<uint64_t>()) {
                    cfg.genesis_allocations[address] = *balance;
                }
            }
        }
    }

    // Validate after loading
    cfg.validate();

    return cfg;
}

void Config::validate() const {
    validate_network_config();
    validate_consensus_config();
    validate_tokenomics_config();
    validate_crypto_config();
}

void Config::validate_network_config() const {
    // Port range validation
    if (listen_port < 1 || listen_port > 65535) {
        throw std::invalid_argument("listen_port must be between 1-65535, got: " + 
                                    std::to_string(listen_port));
    }
    
    if (rpc_port < 1 || rpc_port > 65535) {
        throw std::invalid_argument("rpc_port must be between 1-65535, got: " +
                                    std::to_string(rpc_port));
    }
    
    // Address validation
    if (listen_addr.empty()) {
        throw std::invalid_argument("listen_addr cannot be empty");
    }
    
    // Seeds validation
    for (const auto& seed : network_seeds) {
        if (seed.find(':') == std::string::npos) {
            throw std::invalid_argument("Invalid seed format (expected IP:PORT): " + seed);
        }
    }
}

void Config::validate_consensus_config() const {
    if (validators.empty()) {
        LOG_WARN(chrono_util::LogCategory::GENERAL, "No validators configured. Node will not participate in consensus.");
    }

    // Validate each validator public key format
    for (const auto& val : validators) {
        if (val.size() < 10) {  // Minimum reasonable pubkey size (Base58Check is usually longer)
            throw std::invalid_argument("Invalid validator public key (too short): " + val);
        }
    }

    if (pot_delta_min_ms == 0) {
        throw std::invalid_argument("consensus.pot_delta_min_ms must be > 0");
    }
}

void Config::validate_tokenomics_config() const {
    if (max_total_supply == 0) {
        throw std::invalid_argument("max_total_supply must be > 0");
    }
    
    if (fee_burn_percentage > 100) {
        throw std::invalid_argument("fee_burn_percentage must be <= 100");
    }
    
    if (minting_enabled && initial_block_reward_nanos == 0) {
        LOG_WARN(chrono_util::LogCategory::GENERAL,
                 "minting_enabled but initial_block_reward_nanos is 0");
    }
}

void Config::validate_crypto_config() const {
    // HRP validation
    if (addr_hrp.empty() || addr_hrp.size() > 10) {
        throw std::invalid_argument("addr_hrp must be 1-10 characters");
    }
    
    // Algorithm validation
    std::vector<std::string> valid_algs = {"dilithium_2", "dilithium_3", "dilithium_5", "hmac"};
    if (std::find(valid_algs.begin(), valid_algs.end(), sign_alg) == valid_algs.end()) {
        throw std::invalid_argument("Unknown sign_alg: " + sign_alg);
    }
}

} // namespace chrono_node