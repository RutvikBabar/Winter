#pragma once
#include <winter/strategy/strategy_base.hpp>
#include <winter/core/signal.hpp>
#include <winter/core/market_data.hpp>
#include <deque>
#include <unordered_map>
#include <cmath>
#include <algorithm>

class MeanReversionStrategy : public winter::strategy::StrategyBase {
private:
    struct StockData {
        std::deque<double> prices;
        double sum = 0.0;
        double sum_sq = 0.0;
        int window_size;
        
        explicit StockData(int window = 20) : window_size(window) {}
        
        void add_price(double price) {
            // Add new price
            prices.push_back(price);
            sum += price;
            sum_sq += price * price;
            
            // Remove oldest price if we exceed window size
            if (prices.size() > window_size) {
                double old_price = prices.front();
                prices.pop_front();
                sum -= old_price;
                sum_sq -= old_price * old_price;
            }
        }
        
        double mean() const {
            if (prices.empty()) return 0.0;
            return sum / prices.size();
        }
        
        double std_dev() const {
            if (prices.size() < 2) return 0.0;
            double avg = mean();
            double variance = (sum_sq / prices.size()) - (avg * avg);
            return std::sqrt(std::max(0.0, variance));
        }
        
        double z_score(double current_price) const {
            double std = std_dev();
            if (std == 0.0) return 0.0;
            return (current_price - mean()) / std;
        }
    };
    
    std::unordered_map<std::string, StockData> stock_data_;
    double entry_threshold_ = 2.0;  // Z-score threshold for entry
    double exit_threshold_ = 0.5;   // Z-score threshold for exit
    
public:
    MeanReversionStrategy() : StrategyBase("MeanReversion") {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        // Get or create stock data
        auto& stock = stock_data_[data.symbol];
        
        // Update price history
        stock.add_price(data.price);
        
        // Only generate signals if we have enough data
        if (stock.prices.size() < stock.window_size) {
            return signals;
        }
        
        // Calculate z-score
        double z_score = stock.z_score(data.price);
        
        // Generate signals based on z-score
        if (z_score > entry_threshold_) {
            // Price is too high, generate SELL signal
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::SELL;
            signal.strength = std::min(1.0, (z_score - entry_threshold_) / 2.0);
            signal.price = data.price;
            signals.push_back(signal);
        } 
        else if (z_score < -entry_threshold_) {
            // Price is too low, generate BUY signal
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::BUY;
            signal.strength = std::min(1.0, (-z_score - entry_threshold_) / 2.0);
            signal.price = data.price;
            signals.push_back(signal);
        }
        else if (std::abs(z_score) < exit_threshold_) {
            // Price is close to mean, generate EXIT signal
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::EXIT;
            signal.strength = 1.0 - (std::abs(z_score) / exit_threshold_);
            signal.price = data.price;
            signals.push_back(signal);
        }
        
        return signals;
    }
};
