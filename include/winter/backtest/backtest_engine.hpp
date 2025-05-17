#pragma once

#include <winter/core/engine.hpp>
#include <winter/core/market_data.hpp>
#include <winter/utils/logger.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <string_view>
#include <cstring>
#include <thread>
#include <mutex>
#include <atomic>
#include <future>
#include <execution>
#include <optional>

namespace winter::backtest {

struct PerformanceMetrics {
    double initial_capital;
    double final_capital;
    double total_return;
    double total_return_pct;
    double annualized_return;
    double sharpe_ratio;
    double max_drawdown;
    double max_drawdown_pct;
    int total_trades;
    int winning_trades;
    int losing_trades;
    double win_rate;
    double profit_factor;
    double avg_profit_per_trade;
    double avg_loss_per_trade;
    double avg_trade_duration;
    double max_consecutive_wins;
    double max_consecutive_losses;
};

// Structure to hold equity curve data points
struct EquityPoint {
    int64_t timestamp;
    double equity;
    std::string symbol;
    std::string trade_type; // "BUY", "SELL", or empty for regular equity point
};

// Backtest configuration
struct BacktestConfiguration {
    // Parallelism settings
    size_t thread_count = std::thread::hardware_concurrency();
    size_t batch_size = 10000;
    
    // Engine configuration
    winter::core::EngineConfiguration engine_config;
    
    // Memory settings
    size_t memory_pool_size = 1024 * 1024 * 1024; // 1GB
};

class BacktestEngine {
private:
    winter::core::Engine engine_;
    std::vector<winter::core::MarketData> historical_data_;
    std::vector<EquityPoint> equity_curve_;
    std::vector<std::pair<std::string, double>> daily_returns_;
    std::string start_date_;
    std::string end_date_;
    std::atomic<bool> running_;
    std::atomic<int> processed_count_;
    std::mutex equity_mutex_;
    double initial_balance_;
    BacktestConfiguration config_;
    
    // Thread pool for parallel processing
    std::vector<std::thread> worker_threads_;
    
    // Helper methods
    bool load_csv_data(const std::string& csv_file);
    double calculate_sharpe_ratio(const std::vector<double>& returns, double risk_free_rate = 0.0);
    double calculate_max_drawdown(const std::vector<EquityPoint>& equity_curve);
    void generate_html_report(const std::string& output_file, const PerformanceMetrics& metrics);
    void export_trades_to_csv(const std::string& csv_file);
    
    // Process a chunk of data in parallel
    void process_data_chunk(size_t start, size_t end);
    
public:
    BacktestEngine();
    ~BacktestEngine();
    
    // Configuration
    void configure(const BacktestConfiguration& config);
    
    bool initialize(double initial_capital);
    bool load_data(const std::string& csv_file);
    bool add_strategy(std::shared_ptr<winter::strategy::StrategyBase> strategy);
    
    bool run_backtest();
    void stop_backtest();
    PerformanceMetrics calculate_performance_metrics();
    bool generate_report(const std::string& output_file);
    
    // Progress tracking
    double get_progress() const;
};

} // namespace winter::backtest
