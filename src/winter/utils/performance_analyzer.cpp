#include "winter/utils/performance_analyzer.hpp"
#include <algorithm>
#include <numeric>

namespace winter {
namespace utils {

void PerformanceAnalyzer::add_equity_point(double equity) {
    equity_curve_.push_back(equity);
}

void PerformanceAnalyzer::add_benchmark_point(double benchmark) {
    benchmark_curve_.push_back(benchmark);
}

void PerformanceAnalyzer::add_trade(const Trade& trade) {
    trades_.push_back(trade);
}

std::vector<double> PerformanceAnalyzer::calculate_returns(const std::vector<double>& curve) {
    if (curve.size() < 2) {
        return {};
    }
    
    std::vector<double> returns;
    returns.reserve(curve.size() - 1);
    
    for (size_t i = 1; i < curve.size(); i++) {
        returns.push_back(curve[i] / curve[i-1] - 1.0);
    }
    
    return returns;
}

double PerformanceAnalyzer::calculate_sharpe_ratio(const std::vector<double>& returns) {
    if (returns.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
    double mean = sum / returns.size();
    
    double sq_sum = 0.0;
    for (double r : returns) {
        sq_sum += (r - mean) * (r - mean);
    }
    
    double std_dev = std::sqrt(sq_sum / returns.size());
    
    if (std_dev < 0.000001) {
        return 0.0;
    }
    
    // Annualize
    double annualized_return = mean * trading_days_per_year_;
    double annualized_std_dev = std_dev * std::sqrt(trading_days_per_year_);
    
    return (annualized_return - risk_free_rate_) / annualized_std_dev;
}

double PerformanceAnalyzer::calculate_sortino_ratio(const std::vector<double>& returns) {
    if (returns.empty()) {
        return 0.0;
    }
    
    double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
    double mean = sum / returns.size();
    
    double sq_sum = 0.0;
    int count = 0;
    for (double r : returns) {
        if (r < 0) {
            sq_sum += (r - 0.0) * (r - 0.0);  // Downside deviation only
            count++;
        }
    }
    
    double downside_dev = (count > 0) ? std::sqrt(sq_sum / count) : 0.000001;
    
    if (downside_dev < 0.000001) {
        return 0.0;
    }
    
    // Annualize
    double annualized_return = mean * trading_days_per_year_;
    double annualized_downside_dev = downside_dev * std::sqrt(trading_days_per_year_);
    
    return (annualized_return - risk_free_rate_) / annualized_downside_dev;
}

double PerformanceAnalyzer::calculate_max_drawdown(const std::vector<double>& curve, double& duration) {
    if (curve.size() < 2) {
        duration = 0.0;
        return 0.0;
    }
    
    double max_dd = 0.0;
    double peak = curve[0];
    double max_duration = 0.0;
    double current_duration = 0.0;
    
    for (size_t i = 1; i < curve.size(); i++) {
        if (curve[i] > peak) {
            peak = curve[i];
            current_duration = 0.0;
        } else {
            current_duration += 1.0;
            double dd = (peak - curve[i]) / peak;
            if (dd > max_dd) {
                max_dd = dd;
                max_duration = current_duration;
            }
        }
    }
    
    duration = max_duration;
    return max_dd;
}

PerformanceMetrics PerformanceAnalyzer::calculate_metrics() {
    PerformanceMetrics metrics;
    
    if (equity_curve_.empty()) {
        return metrics;
    }
    
    // Calculate returns
    std::vector<double> returns = calculate_returns(equity_curve_);
    
    // Total return
    metrics.total_return = (equity_curve_.back() / equity_curve_.front()) - 1.0;
    
    // Annualized return
    double years = static_cast<double>(equity_curve_.size()) / trading_days_per_year_;
    metrics.annualized_return = std::pow(1.0 + metrics.total_return, 1.0 / years) - 1.0;
    
    // Sharpe ratio
    metrics.sharpe_ratio = calculate_sharpe_ratio(returns);
    
    // Sortino ratio
    metrics.sortino_ratio = calculate_sortino_ratio(returns);
    
    // Max drawdown
    metrics.max_drawdown = calculate_max_drawdown(equity_curve_, metrics.max_drawdown_duration);
    
    // Calmar ratio
    metrics.calmar_ratio = (metrics.max_drawdown > 0.000001) ? 
                          metrics.annualized_return / metrics.max_drawdown : 0.0;
    
    // Volatility
    double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
    double mean = sum / returns.size();
    
    double sq_sum = 0.0;
    for (double r : returns) {
        sq_sum += (r - mean) * (r - mean);
    }
    
    metrics.volatility = std::sqrt(sq_sum / returns.size()) * std::sqrt(trading_days_per_year_);
    
    // Win rate and profit factor
    int wins = 0;
    double gross_profit = 0.0;
    double gross_loss = 0.0;
    double total_mfe = 0.0;
    double total_mae = 0.0;
    
    for (const auto& trade : trades_) {
        double profit = trade.is_long ? 
                      (trade.exit_price - trade.entry_price) * trade.quantity :
                      (trade.entry_price - trade.exit_price) * trade.quantity;
        
        if (profit > 0) {
            wins++;
            gross_profit += profit;
        } else {
            gross_loss -= profit;  // Convert to positive
        }
        
        total_mfe += trade.max_profit;
        total_mae += trade.max_loss;
    }
    
    metrics.total_trades = trades_.size();
    metrics.win_rate = (metrics.total_trades > 0) ? 
                     static_cast<double>(wins) / metrics.total_trades : 0.0;
    
    metrics.profit_factor = (gross_loss > 0.000001) ? 
                          gross_profit / gross_loss : 0.0;
    
    metrics.avg_mfe = (metrics.total_trades > 0) ? 
                    total_mfe / metrics.total_trades : 0.0;
    
    metrics.avg_mae = (metrics.total_trades > 0) ? 
                    total_mae / metrics.total_trades : 0.0;
    
    // Beta and Alpha (if benchmark data available)
    if (!benchmark_curve_.empty() && benchmark_curve_.size() == equity_curve_.size()) {
        std::vector<double> benchmark_returns = calculate_returns(benchmark_curve_);
        
        if (returns.size() == benchmark_returns.size() && !returns.empty()) {
            // Calculate covariance
            double sum_xy = 0.0;
            double mean_x = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
            double mean_y = std::accumulate(benchmark_returns.begin(), benchmark_returns.end(), 0.0) / benchmark_returns.size();
            
            for (size_t i = 0; i < returns.size(); i++) {
                sum_xy += (returns[i] - mean_x) * (benchmark_returns[i] - mean_y);
            }
            
            double covariance = sum_xy / returns.size();
            
            // Calculate variance of benchmark
            double sum_y_sq = 0.0;
            for (double r : benchmark_returns) {
                sum_y_sq += (r - mean_y) * (r - mean_y);
            }
            
            double variance_y = sum_y_sq / benchmark_returns.size();
            
            // Beta
            metrics.beta = (variance_y > 0.000001) ? covariance / variance_y : 0.0;
            
            // Alpha (annualized)
            double benchmark_return = (benchmark_curve_.back() / benchmark_curve_.front()) - 1.0;
            double benchmark_annualized = std::pow(1.0 + benchmark_return, 1.0 / years) - 1.0;
            
            metrics.alpha = metrics.annualized_return - (risk_free_rate_ + metrics.beta * (benchmark_annualized - risk_free_rate_));
        }
    }
    
    return metrics;
}

} // namespace utils
} // namespace winter
