
uint64_t NodeApp::calculate_block_reward(uint64_t height) const {
    if (!cfg_.minting_enabled || cfg_.reward_halving_interval == 0) {
        return 0;
    }
    
    uint64_t halvings = height / cfg_.reward_halving_interval;
    uint64_t reward = cfg_.initial_block_reward_nanos;
    
    // Apply halving (divide by 2^halvings)
    for (uint64_t i = 0; i < halvings && reward > 1; ++i) {
        reward /= 2;
    }
    return reward;
}

} // namespace chrono_node
