#pragma once
#include <winter/strategy/strategy_base.hpp>
#include <winter/core/signal.hpp>
#include <winter/core/market_data.hpp>
#include <deque>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <numeric>

class MeanReversionStrategy : public winter::strategy::StrategyBase {
private:
    struct StockData {
        // Price data
        std::deque<double> prices;
        double sum = 0.0;
        double sum_sq = 0.0;
        int window_size = 20;
        
        // Volume data
        std::deque<double> volumes;
        double short_volume_ma = 0.0;  // 14-period
        double long_volume_ma = 0.0;   // 28-period
        
        // Trend filter
        double ema_200 = 0.0;
        bool ema_initialized = false;
        
        // Volatility
        double bb_width = 0.0;
        double atr_14 = 0.0;
        std::deque<double> true_ranges;
        
        // Momentum
        double rsi = 50.0;
        std::deque<double> gains;
        std::deque<double> losses;

        explicit StockData() = default;

        void update_indicators(const winter::core::MarketData& data) {
            // Update price and volume data
            prices.push_back(data.price);
            volumes.push_back(data.volume);
            
            // Maintain price window
            if (prices.size() > window_size) {
                double old_price = prices.front();
                prices.pop_front();
                sum -= old_price;
                sum_sq -= old_price * old_price;
            }
            sum += data.price;
            sum_sq += data.price * data.price;

            // Maintain volume window
            if (volumes.size() > 28) volumes.pop_front();

            // Update indicators
            update_volume_oscillator();
            update_ema_200(data.price);
            update_bollinger_bands();
            update_atr(data.price);
            update_rsi(data.price);
        }

    private:
        void update_volume_oscillator() {
            if (volumes.size() >= 28) {
                auto short_start = volumes.end() - 14;
                auto long_start = volumes.end() - 28;
                short_volume_ma = std::accumulate(short_start, volumes.end(), 0.0) / 14;
                long_volume_ma = std::accumulate(long_start, volumes.end(), 0.0) / 28;
            }
        }

        void update_ema_200(double price) {
            const double alpha = 2.0 / (200 + 1);
            if (!ema_initialized) {
                if (prices.size() >= 200) {
                    ema_200 = std::accumulate(prices.begin(), prices.end(), 0.0) / prices.size();
                    ema_initialized = true;
                }
            } else {
                ema_200 = (price - ema_200) * alpha + ema_200;
            }
        }

        void update_bollinger_bands() {
            if (prices.size() >= window_size) {
                double mean = sum / prices.size();
                double variance = (sum_sq / prices.size()) - (mean * mean);
                double std_dev = std::sqrt(std::max(0.0, variance));
                
                bb_width = (2.5 * 2 * std_dev) / mean; // Simplified BB width calculation
            }
        }

        void update_atr(double price) {
            if (prices.size() >= 2) {
                double previous_close = prices[prices.size() - 2];
                double tr = std::abs(price - previous_close);
                
                true_ranges.push_back(tr);
                if (true_ranges.size() > 14) true_ranges.pop_front();
                
                if (true_ranges.size() == 14) {
                    atr_14 = std::accumulate(true_ranges.begin(), true_ranges.end(), 0.0) / 14;
                }
            }
        }

        void update_rsi(double price) {
            if (prices.size() >= 2) {
                double previous_price = prices[prices.size() - 2];
                double change = price - previous_price;
                
                gains.push_back(std::max(change, 0.0));
                losses.push_back(std::max(-change, 0.0));
                
                if (gains.size() > 14) gains.pop_front();
                if (losses.size() > 14) losses.pop_front();
                
                if (gains.size() == 14) {
                    double avg_gain = std::accumulate(gains.begin(), gains.end(), 0.0) / 14;
                    double avg_loss = std::accumulate(losses.begin(), losses.end(), 0.0) / 14;
                    
                    rsi = avg_loss == 0 ? 100.0 : 100.0 - (100.0 / (1 + (avg_gain / avg_loss)));
                }
            }
        }
    };

    std::unordered_map<std::string, StockData> stock_data_;
    double entry_threshold_ = 2.5;  // Z-score threshold for entry
    double exit_threshold_ = 0.5;   // Z-score threshold for exit

public:
    MeanReversionStrategy() : StrategyBase("MeanReversion") {}

    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        auto& stock = stock_data_[data.symbol];
        stock.update_indicators(data);

        if (!ready_for_trading(stock)) return signals;

        double z_score = calculate_z_score(stock, data.price);
        double vol_osc = volume_oscillator(stock);

        // Long entry conditions
        if (z_score <= -entry_threshold_ &&
            stock.bb_width > 0.15 &&
            vol_osc < -30 &&
            data.price > stock.ema_200 &&
            stock.rsi < 35) {
            
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::BUY;
            signal.price = data.price;
            signal.strength = std::min(1.0, (-z_score - entry_threshold_) / 2.0);
            signals.push_back(signal);
        }
        // Short entry conditions
        else if (z_score >= entry_threshold_ &&
                 stock.bb_width > 0.15 &&
                 vol_osc > 30 &&
                 data.price < stock.ema_200 &&
                 stock.rsi > 65) {
            
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::SELL;
            signal.price = data.price;
            signal.strength = std::min(1.0, (z_score - entry_threshold_) / 2.0);
            signals.push_back(signal);
        }
        // Exit conditions
        else if (std::abs(z_score) < exit_threshold_) {
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::EXIT;
            signal.price = data.price;
            signal.strength = 1.0 - (std::abs(z_score) / exit_threshold_);
            signals.push_back(signal);
        }

        return signals;
    }

private:
    bool ready_for_trading(const StockData& stock) const {
        return stock.prices.size() >= stock.window_size &&
               stock.volumes.size() >= 28 &&
               stock.ema_initialized &&
               stock.true_ranges.size() >= 14;
    }

    double calculate_z_score(const StockData& stock, double price) const {
        double mean = stock.sum / stock.prices.size();
        double variance = (stock.sum_sq / stock.prices.size()) - (mean * mean);
        double std_dev = std::sqrt(std::max(0.0, variance));
        return std_dev == 0.0 ? 0.0 : (price - mean) / std_dev;
    }

    double volume_oscillator(const StockData& stock) const {
        if (stock.long_volume_ma == 0) return 0.0;
        return ((stock.short_volume_ma - stock.long_volume_ma) / stock.long_volume_ma) * 100;
    }
};
