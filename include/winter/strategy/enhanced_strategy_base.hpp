// include/winter/strategy/enhanced_strategy_base.hpp
#pragma once
#include "winter/strategy/strategy_base.hpp"
#include "winter/core/signal.hpp"
#include "winter/core/market_data.hpp"
#include "winter/utils/logger.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>

namespace winter {
namespace strategy {

/**
 * @class EnhancedStrategyBase
 * @brief An enhanced base class for implementing trading strategies
 */
class EnhancedStrategyBase : public StrategyBase {
public:
    /**
     * @brief Constructor
     * @param name The strategy name
     */
    EnhancedStrategyBase(const std::string& name) : StrategyBase(name) {
        initialize_common();
    }

    /**
     * @brief Process incoming market data and generate signals
     * @param data The market data tick
     * @return Vector of trading signals
     */
    std::vector<core::Signal> process_tick(const core::MarketData& data) override {
        // Store the latest price
        latest_prices_[data.symbol] = data.price;
        
        // Update price history
        update_price_history(data.symbol, data.price);
        
        // Call the user's strategy logic
        return generate_signals(data);
    }
    
    /**
     * @brief Main strategy logic - override this method
     * @param data The market data tick
     * @return Vector of trading signals
     */
    virtual std::vector<core::Signal> generate_signals(const core::MarketData& data) {
        // Override this method to implement your strategy logic
        return {};
    }

protected:
    // Helper methods for strategy implementation
    
    /**
     * @brief Create a buy signal
     */
    core::Signal create_buy_signal(const std::string& symbol, double price, int quantity = 1) {
        core::Signal signal;
        signal.symbol = symbol;
        signal.type = core::SignalType::BUY;
        signal.price = price;
        signal.strength = 1.0;
        
        // Update position tracking
        positions_[symbol] += quantity;
        
        return signal;
    }
    
    /**
     * @brief Create a sell signal
     */
    core::Signal create_sell_signal(const std::string& symbol, double price, int quantity = 1) {
        core::Signal signal;
        signal.symbol = symbol;
        signal.type = core::SignalType::SELL;
        signal.price = price;
        signal.strength = 1.0;
        
        // Update position tracking
        positions_[symbol] -= quantity;
        
        return signal;
    }
    
    /**
     * @brief Get the current position for a symbol
     */
    int get_position(const std::string& symbol) const {
        auto it = positions_.find(symbol);
        if (it != positions_.end()) {
            return it->second;
        }
        return 0;
    }
    
    /**
     * @brief Get the latest price for a symbol
     */
    double get_latest_price(const std::string& symbol) const {
        auto it = latest_prices_.find(symbol);
        if (it != latest_prices_.end()) {
            return it->second;
        }
        return 0.0;
    }
    
    /**
     * @brief Calculate simple moving average
     */
    double calculate_sma(const std::string& symbol, int period) const {
        auto it = price_history_.find(symbol);
        if (it == price_history_.end() || it->second.size() < period) {
            return 0.0;
        }
        
        const auto& prices = it->second;
        double sum = 0.0;
        for (int i = 0; i < period; ++i) {
            sum += prices[prices.size() - 1 - i];
        }
        
        return sum / period;
    }
    
    /**
     * @brief Log a message with the strategy name
     */
    void log_message(const std::string& message) const {
        utils::Logger::info() << "[" << name() << "] " << message << utils::Logger::endl;
    }

private:
    // Internal state
    std::unordered_map<std::string, int> positions_;
    std::unordered_map<std::string, double> latest_prices_;
    std::unordered_map<std::string, std::deque<double>> price_history_;
    const int MAX_HISTORY_SIZE = 1000;
    
    void initialize_common() {
        // Common initialization for all strategies
    }
    
    void update_price_history(const std::string& symbol, double price) {
        auto& history = price_history_[symbol];
        history.push_back(price);
        if (history.size() > MAX_HISTORY_SIZE) {
            history.pop_front();
        }
    }
};

} // namespace strategy
} // namespace winter
