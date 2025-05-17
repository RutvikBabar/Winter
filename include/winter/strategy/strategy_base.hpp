#pragma once
#include <string>
#include <memory>
#include <vector>
#include <winter/core/market_data.hpp>
#include <winter/core/signal.hpp>

namespace winter::strategy {

class StrategyBase {
protected:
    std::string name_;
    bool enabled_ = true;
    
public:
    explicit StrategyBase(std::string name) : name_(std::move(name)) {}
    virtual ~StrategyBase() = default;
    
    // Core strategy method that must be implemented by derived classes
    virtual std::vector<core::Signal> process_tick(const core::MarketData& data) = 0;
    
    // Lifecycle methods
    virtual void initialize() {}
    virtual void shutdown() {}
    
    // Accessors
    // Accessors can be constexpr
    constexpr const std::string& name() const { return name_; }
    constexpr bool is_enabled() const { return enabled_; }

    void set_enabled(bool enabled) { enabled_ = enabled; }
};

using StrategyPtr = std::shared_ptr<StrategyBase>;

} // namespace winter::strategy
