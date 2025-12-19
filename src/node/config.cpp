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
#include <stdexcept>

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
    
    // Load RPC port from its own section
    if (auto rpc_tbl = tbl["rpc"].as_table()) {
        cfg.rpc_port = (*rpc_tbl)["port"].value_or(cfg.rpc_port);
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
        if (auto gossip_topics_arr = (*p2p_tbl)["gossip_topics"].as_array()) {
            cfg.gossip_topics.clear();
            for (const auto& item : *gossip_topics_arr) {
                if (auto s = item.as<std::string>()) {
                    cfg.gossip_topics.push_back(s->get());
                }
            }
        }
    }

    // --- Consensus settings ---
    if (auto consensus_tbl = tbl["consensus"].as_table()) {
        cfg.bft_round_timeout_ms = (*consensus_tbl)["bft_round_timeout_ms"].value_or(cfg.bft_round_timeout_ms);
        cfg.slot_ms = (*consensus_tbl)["slot_ms"].value_or(cfg.slot_ms);
        cfg.bft_quorum = (*consensus_tbl)["bft_quorum"].value_or(cfg.bft_quorum);
        cfg.outlier_mad_factor = (*consensus_tbl)["outlier_mad_factor"].value_or(cfg.outlier_mad_factor);
        cfg.min_threshold_ms = (*consensus_tbl)["min_threshold_ms"].value_or(cfg.min_threshold_ms);
        
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
    }

    // --- Crypto settings ---
    if (auto crypto_tbl = tbl["crypto"].as_table()) {
        cfg.sign_alg = (*crypto_tbl)["sign_alg"].value_or(cfg.sign_alg);
        cfg.prehash = (*crypto_tbl)["prehash"].value_or(cfg.prehash);
        cfg.addr_hrp = (*crypto_tbl)["addr_hrp"].value_or(cfg.addr_hrp);
        cfg.private_key = (*crypto_tbl)["private_key"].value_or(cfg.private_key);
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

    return cfg;
}

} // namespace chrono_node