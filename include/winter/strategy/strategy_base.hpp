// include/winter/strategy/strategy_base.hpp
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "winter/core/market_data.hpp"
#include "winter/core/signal.hpp"

namespace winter {
namespace strategy {

class StrategyBase {
protected:
    std::string name_;
    bool enabled_ = true;
    std::unordered_map<std::string, std::string> config_;

public:
    explicit StrategyBase(std::string name) : name_(std::move(name)) {}
    virtual ~StrategyBase() = default;

    // Core method that must be implemented by all strategies
    virtual std::vector<core::Signal> process_tick(const core::MarketData& data) = 0;
    
    // Lifecycle methods
    virtual void initialize() {}
    virtual void on_day_start() {}
    virtual void on_day_end() {}
    virtual void shutdown() {}
    
    // Configuration
    virtual void configure(const std::unordered_map<std::string, std::string>& config) {
        config_ = config;
    }
    
    std::string get_config(const std::string& key, const std::string& default_value = "") const {
        auto it = config_.find(key);
        return it != config_.end() ? it->second : default_value;
    }

    // Accessors
    const std::string& name() const { return name_; }
    bool is_enabled() const { return enabled_; }
    void set_enabled(bool enabled) { enabled_ = enabled; }
};

using StrategyPtr = std::shared_ptr<StrategyBase>;

} // namespace strategy
} // namespace winter
