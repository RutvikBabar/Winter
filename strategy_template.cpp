#pragma once

#include <winter/strategy/strategy_base.hpp>
#include <winter/core/signal.hpp>
#include <winter/core/market_data.hpp>
#include <winter/utils/logger.hpp>
#include <vector>
#include <string>
#include <unordered_map>
#include <deque>
#include <memory>

namespace winter {
namespace strategy {

/**
 * @class EnhancedStrategyBase
 * @brief An enhanced base class for implementing trading strategies
 * 
 * This class provides a structured framework for implementing trading strategies
 * with common functionality like position tracking, signal generation, and performance metrics.
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
     * @brief Initialize strategy parameters
     * Override this method to set up your strategy's parameters
     */
    void initialize() override {
        // Override to initialize strategy-specific parameters
    }

    /**
     * @brief Process incoming market data and generate signals
     * @param data The market data tick
     * @return Vector of trading signals
     */
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
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
    virtual std::vector<winter::core::Signal> generate_signals(const winter::core::MarketData& data) {
        // Override this method to implement your strategy logic
        return {};
    }

    /**
     * @brief Handle end of day processing
     * Override for EOD tasks like position rebalancing or reporting
     */
    virtual void on_day_end() {
        // Override for EOD tasks
    }

    /**
     * @brief Reset strategy state
     * Override to reset any internal state
     */
    void shutdown() override {
        positions_.clear();
        latest_prices_.clear();
        price_history_.clear();
    }

protected:
    // Helper methods for strategy implementation
    
    /**
     * @brief Create a buy signal
     * @param symbol The symbol to buy
     * @param price The price to buy at
     * @param quantity The quantity to buy
     * @return A buy signal
     */
    winter::core::Signal create_buy_signal(const std::string& symbol, double price, int quantity = 1) {
        winter::core::Signal signal;
        signal.symbol = symbol;
        signal.type = winter::core::SignalType::BUY;
        signal.price = price;
        signal.strength = 1.0;
        
        // Update position tracking
        positions_[symbol] += quantity;
        
        return signal;
    }
    
    /**
     * @brief Create a sell signal
     * @param symbol The symbol to sell
     * @param price The price to sell at
     * @param quantity The quantity to sell
     * @return A sell signal
     */
    winter::core::Signal create_sell_signal(const std::string& symbol, double price, int quantity = 1) {
        winter::core::Signal signal;
        signal.symbol = symbol;
        signal.type = winter::core::SignalType::SELL;
        signal.price = price;
        signal.strength = 1.0;
        
        // Update position tracking
        positions_[symbol] -= quantity;
        
        return signal;
    }
    
    /**
     * @brief Get the current position for a symbol
     * @param symbol The symbol to check
     * @return The current position (positive for long, negative for short, 0 for flat)
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
     * @param symbol The symbol to check
     * @return The latest price, or 0.0 if not available
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
     * @param symbol The symbol to calculate for
     * @param period The period for the moving average
     * @return The SMA value, or 0.0 if insufficient data
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
     * @brief Calculate exponential moving average
     * @param symbol The symbol to calculate for
     * @param period The period for the moving average
     * @return The EMA value, or 0.0 if insufficient data
     */
    double calculate_ema(const std::string& symbol, int period) const {
        auto it = price_history_.find(symbol);
        if (it == price_history_.end() || it->second.size() < period) {
            return 0.0;
        }
        
        const auto& prices = it->second;
        double alpha = 2.0 / (period + 1.0);
        double ema = prices[0];
        
        for (size_t i = 1; i < prices.size(); ++i) {
            ema = alpha * prices[i] + (1.0 - alpha) * ema;
        }
        
        return ema;
    }
    
    /**
     * @brief Log a message with the strategy name
     * @param message The message to log
     */
    void log_message(const std::string& message) const {
        winter::utils::Logger::info() << "[" << name() << "] " << message << winter::utils::Logger::endl;
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
