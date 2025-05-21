// examples/simple_ma_strategy/simple_ma_strategy.hpp
#pragma once
#include "winter/strategy/enhanced_strategy_base.hpp"
#include "winter/strategy/strategy_factory.hpp"

namespace winter {
namespace examples {

class SimpleMAStrategy : public strategy::EnhancedStrategyBase {
public:
    SimpleMAStrategy() : EnhancedStrategyBase("SimpleMAStrategy") {}
    
    void initialize() override {
        // Get parameters from configuration
        fast_period_ = std::stoi(get_config("fast_period", "10"));
        slow_period_ = std::stoi(get_config("slow_period", "30"));
        
        log_message("Initialized with fast_period=" + std::to_string(fast_period_) + 
                   ", slow_period=" + std::to_string(slow_period_));
    }
    
    std::vector<core::Signal> generate_signals(const core::MarketData& data) override {
        std::vector<core::Signal> signals;
        
        // Calculate moving averages
        double fast_ma = calculate_sma(data.symbol, fast_period_);
        double slow_ma = calculate_sma(data.symbol, slow_period_);
        
        // Skip if we don't have enough data
        if (fast_ma == 0.0 || slow_ma == 0.0) {
            return signals;
        }
        
        // Get current position
        int position = get_position(data.symbol);
        
        // Generate signals based on moving average crossover
        if (fast_ma > slow_ma && position <= 0) {
            // Buy signal
            signals.push_back(create_buy_signal(data.symbol, data.price));
            log_message("BUY signal for " + data.symbol + " at " + std::to_string(data.price));
        } else if (fast_ma < slow_ma && position >= 0) {
            // Sell signal
            signals.push_back(create_sell_signal(data.symbol, data.price));
            log_message("SELL signal for " + data.symbol + " at " + std::to_string(data.price));
        }
        
        return signals;
    }
    
private:
    int fast_period_ = 10;
    int slow_period_ = 30;
};

// Register the strategy with the factory
namespace {
    bool registered = []() {
        strategy::StrategyFactory::register_type<SimpleMAStrategy>("SimpleMAStrategy");
        return true;
    }();
}

} // namespace examples
} // namespace winter
