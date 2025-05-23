// include/winter/utils/performance_analyzer.hpp
#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <cmath>

namespace winter {
namespace utils {

struct PerformanceMetrics {
    double total_return;
    double annualized_return;
    double sharpe_ratio;
    double sortino_ratio;
    double max_drawdown;
    double max_drawdown_duration;
    double win_rate;
    double profit_factor;
    double calmar_ratio;
    double volatility;
    double beta;
    double alpha;
    double avg_mfe;
    double avg_mae;
    int total_trades;
};

struct Trade {
    std::string symbol;
    double entry_price;
    double exit_price;
    double entry_time;
    double exit_time;
    double quantity;
    double max_profit;  // For MFE
    double max_loss;    // For MAE
    bool is_long;
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
};

} // namespace utils
} // namespace winter
