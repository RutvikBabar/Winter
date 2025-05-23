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
#include <deque>
#include <unordered_map>

namespace winter::backtest {

struct Trade {
    std::string symbol;
    double entry_price = 0.0;
    double exit_price = 0.0;
    int64_t entry_time = 0;
    int64_t exit_time = 0;
    double quantity = 0.0;
    double max_profit = 0.0;  // For MFE
    double max_loss = 0.0;    // For MAE
    bool is_long = true;
};

struct PerformanceMetrics {
    double initial_capital = 0.0;
    double final_capital = 0.0;
    double total_return = 0.0;
    double total_return_pct = 0.0;
    double annualized_return = 0.0;
    double sharpe_ratio = 0.0;
    double sortino_ratio = 0.0;
    double max_drawdown = 0.0;
    double max_drawdown_pct = 0.0;
    double max_drawdown_duration = 0.0;
    int total_trades = 0;
    int winning_trades = 0;
    int losing_trades = 0;
    double win_rate = 0.0;
    double profit_factor = 0.0;
    double calmar_ratio = 0.0;
    double volatility = 0.0;
    double beta = 0.0;
    double alpha = 0.0;
    double avg_mfe = 0.0;
    double avg_mae = 0.0;
    double avg_profit_per_trade = 0.0;
    double avg_loss_per_trade = 0.0;
    double avg_trade_duration = 0.0;
    double max_consecutive_wins = 0;
    double max_consecutive_losses = 0;
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
    
    // Data settings
    bool use_adjusted_prices = true;
    bool fill_missing_data = true;
    std::string missing_data_method = "forward_fill"; // forward_fill, backward_fill, interpolate
    
    // Time settings
    std::string start_time = "09:30:00";
    std::string end_time = "16:00:00";
    bool skip_weekends = true;
    bool skip_holidays = true;
    std::vector<std::string> holiday_dates;
    
    // Execution settings
    double slippage = 0.0; // In percentage
    double commission = 0.0; // In percentage
    double market_impact = 0.0; // In percentage
    
    // Reporting settings
    bool generate_html_report = true;
    bool generate_csv_report = true;
    bool generate_equity_curve = true;
    bool generate_trade_list = true;
    std::string output_directory = "./backtest_results";
};

class PerformanceAnalyzer {
private:
    std::vector<double> equity_curve_;
    std::vector<double> benchmark_curve_;
    std::vector<Trade> trades_;
    double initial_capital_;
    double risk_free_rate_;
    int trading_days_per_year_ = 252;

public:
    PerformanceAnalyzer(double initial_capital = 100000.0, double risk_free_rate = 0.0)
        : initial_capital_(initial_capital), risk_free_rate_(risk_free_rate) {}
    
    void add_equity_point(double equity);
    void add_benchmark_point(double benchmark);
    void add_trade(const Trade& trade);
    
    PerformanceMetrics calculate_metrics();
    
    // Helper methods
    double calculate_sharpe_ratio(const std::vector<double>& returns);
    double calculate_sortino_ratio(const std::vector<double>& returns);
    double calculate_max_drawdown(const std::vector<double>& curve, double& duration);
    std::vector<double> calculate_returns(const std::vector<double>& curve);
    
    // Getters
    const std::vector<double>& get_equity_curve() const { return equity_curve_; }
    const std::vector<double>& get_benchmark_curve() const { return benchmark_curve_; }
    const std::vector<Trade>& get_trades() const { return trades_; }
};

class BacktestEngine {
private:
    winter::core::Engine engine_;
    std::vector<winter::core::MarketData> historical_data_;
    std::vector<EquityPoint> equity_curve_;
    std::vector<std::vector<double>> daily_returns_;
    std::string start_date_;
    std::string end_date_;
    std::atomic<bool> running_{false};
    std::atomic<size_t> processed_count_{0};
    std::mutex equity_mutex_;
    double initial_balance_;
    BacktestConfiguration config_;
    PerformanceAnalyzer performance_analyzer_;
    
    // Thread pool for parallel processing
    std::vector<std::thread> worker_threads_;
    
    // Active trades tracking
    std::unordered_map<std::string, Trade> active_trades_;
    std::vector<Trade> completed_trades_;
    std::mutex trades_mutex_;
    
    // Helper methods
    bool load_csv_data(const std::string& csv_file);
    bool load_parquet_data(const std::string& parquet_file); // NEW
    double calculate_sharpe_ratio(const std::vector<double>& returns, double risk_free_rate = 0.0);
    double calculate_max_drawdown(const std::vector<EquityPoint>& equity_curve, double& duration);
    void generate_html_report(const std::string& output_file, const PerformanceMetrics& metrics);
    void export_trades_to_csv(const std::string& csv_file);
    
    // Process a chunk of data in parallel
    void process_data_chunk(size_t start, size_t end);
    
    // Event handlers
    void on_order_executed(const winter::core::Order& order);
    void on_market_data_processed(const winter::core::MarketData& data);
    
    // Update MFE and MAE for active trades
    void update_trade_metrics(const std::string& symbol, double price);

public:
    BacktestEngine();
    ~BacktestEngine();
    
    // Configuration
    void configure(const BacktestConfiguration& config);
    const BacktestConfiguration& get_config() const { return config_; }
    
    // Initialization
    bool initialize(double initial_capital);
    bool load_data(const std::string& data_file);
    bool add_strategy(std::shared_ptr<winter::strategy::StrategyBase> strategy);
    
    // Execution
    bool run_backtest();
    void stop_backtest();
    
    // Results
    PerformanceMetrics calculate_performance_metrics();
    bool generate_report(const std::string& output_file);
    const std::vector<EquityPoint>& get_equity_curve() const { return equity_curve_; }
    const std::vector<Trade>& get_completed_trades() const { return completed_trades_; }
    
    // Progress tracking
    double get_progress() const;
};

} // namespace winter::backtest
