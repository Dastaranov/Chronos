
bool State::validate_total_supply(uint64_t max_supply) const {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    return total_circulating_supply_ <= max_supply;
}

void State::update_circulating_supply(int64_t delta) {
    std::lock_guard<std::mutex> lock(balances_mutex_);
    if (delta > 0) {
        total_circulating_supply_ += delta;
    } else {
        uint64_t burn = -delta;
        if (total_circulating_supply_ >= burn) {
            total_circulating_supply_ -= burn;
        } else {
            total_circulating_supply_ = 0;
        }
    }
}

} // namespace chrono_ledger
