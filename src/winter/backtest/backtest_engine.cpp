#include <winter/backtest/backtest_engine.hpp>
#include <iomanip>
#include <ctime>
#include <numeric>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>
#include <execution>
#include <csignal>
#include <filesystem>
#include <iostream>

namespace winter::backtest {

BacktestEngine::BacktestEngine() : running_(false), processed_count_(0) {
    // Set default configuration
    config_.thread_count = std::thread::hardware_concurrency();
    config_.batch_size = 10000;
    
    // Configure engine with larger queue sizes
    config_.engine_config.market_data_queue_size = 100000;
    config_.engine_config.order_queue_size = 50000;
    config_.engine_config.batch_size = 1000;
    
    engine_.configure(config_.engine_config);
    
    // Register signal handler
    std::signal(SIGINT, [](int signal) {
        winter::utils::Logger::info() << "Received interrupt signal. Stopping backtest..." << winter::utils::Logger::endl;
    });
}

BacktestEngine::~BacktestEngine() {
    stop_backtest();
}

void BacktestEngine::configure(const BacktestConfiguration& config) {
    config_ = config;
    engine_.configure(config_.engine_config);
}

bool BacktestEngine::initialize(double initial_capital) {
    engine_.portfolio().set_cash(initial_capital);
    initial_balance_ = initial_capital;
    equity_curve_.clear();
    
    // Create initial equity point
    EquityPoint initial_point;
    initial_point.timestamp = 0;
    initial_point.equity = initial_capital;
    initial_point.symbol = "";
    initial_point.trade_type = "";
    equity_curve_.push_back(initial_point);
    
    daily_returns_.clear();
    processed_count_ = 0;
    return true;
}

bool BacktestEngine::load_data(const std::string& csv_file) {
    return load_csv_data(csv_file);
}

bool BacktestEngine::add_strategy(std::shared_ptr<winter::strategy::StrategyBase> strategy) {
    engine_.add_strategy(strategy);
    return true;
}

bool BacktestEngine::load_csv_data(const std::string& csv_file) {
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Check if file exists
    if (!std::filesystem::exists(csv_file)) {
        winter::utils::Logger::error() << "CSV file does not exist: " << csv_file << winter::utils::Logger::endl;
        return false;
    }
    
    // Get file size for progress reporting
    size_t file_size = std::filesystem::file_size(csv_file);
    
    // Open file
    std::ifstream file(csv_file);
    if (!file.is_open()) {
        winter::utils::Logger::error() << "Failed to open CSV file: " << csv_file << winter::utils::Logger::endl;
        return false;
    }
    
    // Clear existing data
    historical_data_.clear();
    
    // Reserve memory based on estimated line count (assume average line length of 100 bytes)
    size_t estimated_lines = file_size / 100;
    historical_data_.reserve(estimated_lines);
    
    std::string line;
    // Skip header line
    std::getline(file, line);
    
    // Read all lines into a buffer for parallel processing
    std::vector<std::string> lines;
    lines.reserve(estimated_lines);
    
    winter::utils::Logger::info() << "Reading CSV file..." << winter::utils::Logger::endl;
    
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    
    winter::utils::Logger::info() << "Read " << lines.size() << " lines from CSV file" << winter::utils::Logger::endl;
    
    // Define a lambda for parsing a single line
    auto parse_line = [](const std::string& line) -> std::optional<winter::core::MarketData> {
        std::stringstream ss(line);
        std::string time, symbol, market_center, price_str, size_str;
        std::string cum_bats_vol, cum_sip_vol, sip_complete, last_sale;
        
        // Parse CSV columns: Time,Symbol,Market Center,Price,Size,...
        std::getline(ss, time, ',');
        std::getline(ss, symbol, ',');
        std::getline(ss, market_center, ',');
        std::getline(ss, price_str, ',');
        std::getline(ss, size_str, ',');
        std::getline(ss, cum_bats_vol, ',');
        std::getline(ss, cum_sip_vol, ',');
        std::getline(ss, sip_complete, ',');
        std::getline(ss, last_sale, ',');
        
        if (time.empty() || symbol.empty() || price_str.empty() || size_str.empty()) {
            return std::nullopt;
        }
        
        try {
            double price = std::stod(price_str);
            int volume = std::stoi(size_str);
            
            winter::core::MarketData data;
            data.symbol = symbol;
            data.price = price;
            data.volume = volume;
            
            // Use line index as timestamp for simplicity
            static std::atomic<int64_t> timestamp_counter(0);
            data.timestamp = timestamp_counter++;
            
            return data;
        } catch (const std::exception& e) {
            // Don't log every parsing error to avoid flooding the console
            return std::nullopt;
        }
    };
    
    winter::utils::Logger::info() << "Parsing CSV data in parallel..." << winter::utils::Logger::endl;
    
    // Process lines in batches to avoid excessive memory usage
    const size_t BATCH_SIZE = 100000;
    size_t total_processed = 0;
    
    for (size_t batch_start = 0; batch_start < lines.size(); batch_start += BATCH_SIZE) {
        size_t batch_end = std::min(batch_start + BATCH_SIZE, lines.size());
        size_t batch_size = batch_end - batch_start;
        
        std::vector<std::optional<winter::core::MarketData>> results(batch_size);
        
        // Parse lines in parallel
        std::transform(
            std::execution::par_unseq,
            lines.begin() + batch_start, 
            lines.begin() + batch_end,
            results.begin(),
            parse_line
        );
        
        // Reserve space for valid results
        size_t valid_count = 0;
        for (const auto& result : results) {
            if (result) valid_count++;
        }
        
        // Add valid results to historical data
        historical_data_.reserve(historical_data_.size() + valid_count);
        for (auto& result : results) {
            if (result) {
                historical_data_.push_back(*result);
                total_processed++;
            }
        }
        
        // Report progress
        double progress = static_cast<double>(batch_end) / lines.size() * 100.0;
        winter::utils::Logger::info() << "Parsing progress: " << std::fixed << std::setprecision(1) 
                                     << progress << "% (" << total_processed << " valid data points)" 
                                     << winter::utils::Logger::endl;
    }
    
    winter::utils::Logger::info() << "Sorting data by timestamp..." << winter::utils::Logger::endl;
    
    // Sort data by timestamp to ensure chronological order
    std::sort(std::execution::par_unseq, historical_data_.begin(), historical_data_.end(),
              [](const winter::core::MarketData& a, const winter::core::MarketData& b) {
                  return a.timestamp < b.timestamp;
              });
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    winter::utils::Logger::info() << "Loaded " << historical_data_.size() << " data points from " 
                                 << lines.size() << " total lines in " << csv_file 
                                 << " (" << duration << "ms)" << winter::utils::Logger::endl;
    
    // Set date range (placeholder - in a real implementation, extract from data)
    start_date_ = "2021-01-01";
    end_date_ = "2021-12-31";
    
    return !historical_data_.empty();
}

void BacktestEngine::process_data_chunk(size_t start, size_t end) {
    // Add thread ID logging
    std::thread::id thread_id = std::this_thread::get_id();
    winter::utils::Logger::info() << "Thread " << thread_id << " starting to process chunk from " 
                                 << start << " to " << end << winter::utils::Logger::endl;
    
    // Process data in batches for better performance
    const size_t BATCH_SIZE = config_.batch_size;
    size_t processed_in_this_chunk = 0;
    
    for (size_t batch_start = start; batch_start < end && running_; batch_start += BATCH_SIZE) {
        size_t batch_end = std::min(batch_start + BATCH_SIZE, end);
        size_t batch_size = batch_end - batch_start;
        
        // Create a batch of market data
        std::vector<winter::core::MarketData> batch(
            historical_data_.begin() + batch_start,
            historical_data_.begin() + batch_end
        );
        
        // Process the batch
        engine_.process_market_data_batch(batch);
        
        // Update equity curve (thread-safe)
        {
            std::lock_guard<std::mutex> lock(equity_mutex_);
            
            // Add a single equity point for the batch
            EquityPoint point;
            point.timestamp = historical_data_[batch_end - 1].timestamp;
            point.equity = engine_.portfolio().total_value();
            point.symbol = "";
            point.trade_type = "";
            equity_curve_.push_back(point);
        }
        
        // Update processed count
        processed_count_ += batch_size;
        processed_in_this_chunk += batch_size;
        
        // Log progress periodically
        if (processed_in_this_chunk % (BATCH_SIZE * 10) == 0) {
            winter::utils::Logger::info() << "Thread " << thread_id << " processed " 
                                         << processed_in_this_chunk << " data points ("
                                         << (static_cast<double>(processed_in_this_chunk) / (end - start) * 100.0)
                                         << "% of assigned chunk)" << winter::utils::Logger::endl;
        }
    }
    
    winter::utils::Logger::info() << "Thread " << thread_id << " finished processing " 
                                 << processed_in_this_chunk << " data points" << winter::utils::Logger::endl;
}

bool BacktestEngine::run_backtest() {
    if (historical_data_.empty()) {
        winter::utils::Logger::error() << "No historical data loaded for backtest" << winter::utils::Logger::endl;
        return false;
    }
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Start the engine
    engine_.start(0, 1);
    
    // Set running flag
    running_ = true;
    processed_count_ = 0;
    
    // Setup order callback to record trades in equity curve
    engine_.set_order_callback([&](const winter::core::Order& order) {
        std::lock_guard<std::mutex> lock(equity_mutex_);
        
        // Add trade point to equity curve
        EquityPoint point;
        point.timestamp = static_cast<int64_t>(processed_count_); // Use current processed count as timestamp
        point.equity = engine_.portfolio().total_value();
        point.symbol = order.symbol;
        point.trade_type = order.side == winter::core::OrderSide::BUY ? "BUY" : "SELL";
        equity_curve_.push_back(point);
    });
    
    // Determine optimal chunk size and thread count
    size_t data_size = historical_data_.size();
    size_t thread_count = config_.thread_count;
    size_t chunk_size = data_size / thread_count;
    
    winter::utils::Logger::info() << "Starting backtest with " << thread_count << " threads, processing " 
                                 << data_size << " data points in chunks of " << chunk_size << winter::utils::Logger::endl;
    
    // Setup progress reporting thread
    std::thread progress_thread([&]() {
        size_t last_processed = 0;
        auto start_time = std::chrono::high_resolution_clock::now();
        
        while (running_ && processed_count_ < historical_data_.size()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            size_t current_processed = processed_count_;
            size_t points_per_second = current_processed - last_processed;
            last_processed = current_processed;
            
            double progress = static_cast<double>(current_processed) / historical_data_.size() * 100.0;
            
            // Calculate estimated time remaining
            auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
            auto elapsed_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
            
            double points_remaining = historical_data_.size() - current_processed;
            double estimated_seconds_remaining = (points_per_second > 0) ? 
                                               points_remaining / points_per_second : 0;
            
            std::cout << "\rProgress: " << std::fixed << std::setprecision(1) << progress 
                      << "% (" << current_processed << "/" << historical_data_.size() 
                      << " points, " << points_per_second << " points/sec, ETA: " 
                      << static_cast<int>(estimated_seconds_remaining) << "s)" << std::flush;
        }
        
        std::cout << "\rProgress: 100.0% (Complete)" << std::endl;
    });
    
    // Process data in parallel chunks
    std::vector<std::future<void>> futures;
    for (size_t t = 0; t < thread_count; ++t) {
        size_t start = t * chunk_size;
        size_t end = (t == thread_count - 1) ? data_size : (t + 1) * chunk_size;
        
        futures.push_back(std::async(std::launch::async, 
            [this, start, end]() { this->process_data_chunk(start, end); }
        ));
    }
    
    // Wait for all threads to complete or for interruption
    for (auto& future : futures) {
        future.wait();
    }
    
    // Set running flag to false to stop progress thread
    running_ = false;
    
    // Join progress thread
    if (progress_thread.joinable()) {
        progress_thread.join();
    }
    
    // Stop the engine
    engine_.stop();
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    winter::utils::Logger::info() << "Backtest completed in " << duration << "ms" << winter::utils::Logger::endl;
    
    return true;
}

void BacktestEngine::stop_backtest() {
    running_ = false;
    
    // Wait for all worker threads to finish
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    worker_threads_.clear();
}

double BacktestEngine::get_progress() const {
    if (historical_data_.empty()) return 0.0;
    return static_cast<double>(processed_count_) / historical_data_.size();
}

PerformanceMetrics BacktestEngine::calculate_performance_metrics() {
    PerformanceMetrics metrics;
    
    // Basic metrics
    metrics.initial_capital = initial_balance_;
    metrics.final_capital = equity_curve_.empty() ? initial_balance_ : equity_curve_.back().equity;
    metrics.total_return = metrics.final_capital - metrics.initial_capital;
    metrics.total_return_pct = (metrics.initial_capital != 0) ? 
                              (metrics.total_return / metrics.initial_capital) * 100.0 : 0.0;
    
    // Calculate trading days (approximation based on timestamps)
    // In a real implementation, extract actual dates from timestamps
    int trading_days = 252;  // Default to 1 year of trading
    double years = trading_days / 252.0;
    
    // Annualized return
    metrics.annualized_return = (years > 0 && metrics.initial_capital > 0) ?
                               std::pow((metrics.final_capital / metrics.initial_capital), (1.0 / years)) - 1.0 : 0.0;
    
    // Extract daily returns
    // In a real implementation, group equity points by day and calculate daily returns
    std::vector<double> returns;
    double prev_equity = initial_balance_;
    for (size_t i = 1; i < equity_curve_.size(); ++i) {
        // Only consider points at day boundaries
        if (i % 1000 == 0) {  // Simplified - use actual day boundaries in real implementation
            double current_equity = equity_curve_[i].equity;
            double daily_return = (current_equity / prev_equity) - 1.0;
            returns.push_back(daily_return);
            prev_equity = current_equity;
        }
    }
    
    // Sharpe ratio
    metrics.sharpe_ratio = calculate_sharpe_ratio(returns);
    
    // Maximum drawdown
    metrics.max_drawdown = calculate_max_drawdown(equity_curve_);
    metrics.max_drawdown_pct = (metrics.initial_capital != 0) ?
                              (metrics.max_drawdown / metrics.initial_capital) * 100.0 : 0.0;
    
    // Trade statistics
    auto& trades = engine_.portfolio().get_trades();
    metrics.total_trades = trades.size();
    
    double total_profit = 0.0;
    double total_loss = 0.0;
    metrics.winning_trades = 0;
    metrics.losing_trades = 0;
    
    int consecutive_wins = 0;
    int consecutive_losses = 0;
    int max_consecutive_wins = 0;
    int max_consecutive_losses = 0;
    
    for (const auto& trade : trades) {
        if (trade.profit > 0) {
            total_profit += trade.profit;
            metrics.winning_trades++;
            consecutive_wins++;
            consecutive_losses = 0;
            max_consecutive_wins = std::max(max_consecutive_wins, consecutive_wins);
        } else {
            total_loss += std::abs(trade.profit);
            metrics.losing_trades++;
            consecutive_losses++;
            consecutive_wins = 0;
            max_consecutive_losses = std::max(max_consecutive_losses, consecutive_losses);
        }
    }
    
    metrics.max_consecutive_wins = max_consecutive_wins;
    metrics.max_consecutive_losses = max_consecutive_losses;
    
    metrics.win_rate = metrics.total_trades > 0 ? 
        static_cast<double>(metrics.winning_trades) / metrics.total_trades : 0.0;
    
    metrics.profit_factor = total_loss > 0 ? total_profit / total_loss : 0.0;
    
    metrics.avg_profit_per_trade = metrics.winning_trades > 0 ? 
        total_profit / metrics.winning_trades : 0.0;
    
    metrics.avg_loss_per_trade = metrics.losing_trades > 0 ? 
        total_loss / metrics.losing_trades : 0.0;
    
    // Average trade duration (placeholder - calculate from actual trade timestamps)
    metrics.avg_trade_duration = 0.0;
    
    return metrics;
}

double BacktestEngine::calculate_sharpe_ratio(const std::vector<double>& returns, double risk_free_rate) {
    if (returns.empty()) {
        return 0.0;
    }
    
    // Calculate average return
    double avg_return = std::accumulate(returns.begin(), returns.end(), 0.0) / returns.size();
    
    // Calculate standard deviation
    double sum_sq_diff = 0.0;
    for (double ret : returns) {
        double diff = ret - avg_return;
        sum_sq_diff += diff * diff;
    }
    
    double std_dev = std::sqrt(sum_sq_diff / returns.size());
    
    // Annualize (assuming daily returns)
    double annualized_return = (avg_return * 252.0);
    double annualized_std_dev = std_dev * std::sqrt(252.0);
    
    // Calculate Sharpe ratio
    return annualized_std_dev > 0 ? 
        (annualized_return - risk_free_rate) / annualized_std_dev : 0.0;
}

double BacktestEngine::calculate_max_drawdown(const std::vector<EquityPoint>& equity_curve) {
    double max_drawdown = 0.0;
    double peak = equity_curve.empty() ? 0.0 : equity_curve[0].equity;
    
    for (const auto& point : equity_curve) {
        if (point.equity > peak) {
            peak = point.equity;
        }
        
        double drawdown = peak - point.equity;
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
        }
    }
    
    return max_drawdown;
}

bool BacktestEngine::generate_report(const std::string& output_file) {
    // Calculate performance metrics
    PerformanceMetrics metrics = calculate_performance_metrics();
    
    // Generate HTML report
    generate_html_report(output_file, metrics);
    
    // Export trades to CSV
    export_trades_to_csv(output_file + ".csv");
    
    return true;
}

void BacktestEngine::export_trades_to_csv(const std::string& csv_file) {
    std::ofstream file(csv_file);
    if (!file.is_open()) {
        winter::utils::Logger::error() << "Failed to create CSV file: " << csv_file << winter::utils::Logger::endl;
        return;
    }
    
    // Write header
    file << "Timestamp,Symbol,Side,Quantity,Price,Value,Profit/Loss" << std::endl;
    
    // Write trades
    auto& trades = engine_.portfolio().get_trades();
    for (const auto& trade : trades) {
        file << trade.timestamp << ","
             << trade.symbol << ","
             << trade.side << ","
             << trade.quantity << ","
             << std::fixed << std::setprecision(2) << trade.price << ","
             << std::fixed << std::setprecision(2) << (trade.quantity * trade.price) << ",";
        
        if (trade.side == "SELL") {
            file << std::fixed << std::setprecision(2) << trade.profit;
        }
        file << std::endl;
    }
    
    file.close();
    winter::utils::Logger::info() << "Exported trades to CSV: " << csv_file << winter::utils::Logger::endl;
}

void BacktestEngine::generate_html_report(const std::string& output_file, const PerformanceMetrics& metrics) {
    std::ofstream html_file(output_file);
    if (!html_file.is_open()) {
        winter::utils::Logger::error() << "Failed to create HTML report file: " << output_file << winter::utils::Logger::endl;
        return;
    }
    
    // Generate equity curve data for the chart
    std::stringstream timestamps_json, equity_json, trades_json;
    timestamps_json << "[";
    equity_json << "[";
    trades_json << "[";
    
    // Use a subset of points for the chart to keep it manageable
    size_t step = std::max(size_t(1), equity_curve_.size() / 1000);
    
    // Buy and sell markers
    std::stringstream buy_points, sell_points;
    buy_points << "[";
    sell_points << "[";
    
    bool first_buy = true;
    bool first_sell = true;
    
    for (size_t i = 0; i < equity_curve_.size(); i += step) {
        if (i > 0) {
            timestamps_json << ",";
            equity_json << ",";
        }
        
        timestamps_json << "\"" << equity_curve_[i].timestamp << "\"";
        equity_json << equity_curve_[i].equity;
        
        // Add trade markers
        if (!equity_curve_[i].trade_type.empty()) {
            if (equity_curve_[i].trade_type == "BUY") {
                if (!first_buy) buy_points << ",";
                buy_points << "{x:" << equity_curve_[i].timestamp 
                          << ",y:" << equity_curve_[i].equity 
                          << ",symbol:'" << equity_curve_[i].symbol << "'}";
                first_buy = false;
            } else if (equity_curve_[i].trade_type == "SELL") {
                if (!first_sell) sell_points << ",";
                sell_points << "{x:" << equity_curve_[i].timestamp 
                           << ",y:" << equity_curve_[i].equity 
                           << ",symbol:'" << equity_curve_[i].symbol << "'}";
                first_sell = false;
            }
        }
    }
    
    timestamps_json << "]";
    equity_json << "]";
    buy_points << "]";
    sell_points << "]";
    
    // Write HTML with embedded Chart.js
    html_file << R"(
<!DOCTYPE html>
<html>
<head>
    <title>Winter Backtest Results</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <script src="https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@1.0.2"></script>
    <style>
        body {
            font-family: Arial, sans-serif;
            margin: 20px;
            background-color: #f5f5f5;
        }
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background-color: white;
            padding: 20px;
            border-radius: 5px;
            box-shadow: 0 0 10px rgba(0,0,0,0.1);
        }
        .header {
            text-align: center;
            margin-bottom: 30px;
        }
        .chart-container {
            height: 500px;
            margin-bottom: 30px;
        }
        .metrics-container {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-between;
        }
        .metric-box {
            width: 30%;
            margin-bottom: 20px;
            padding: 15px;
            border-radius: 5px;
            background-color: #f9f9f9;
            box-shadow: 0 0 5px rgba(0,0,0,0.05);
        }
        .metric-title {
            font-weight: bold;
            margin-bottom: 5px;
            color: #333;
        }
        .metric-value {
            font-size: 20px;
            color: #0066cc;
        }
        .positive {
            color: #00aa00;
        }
        .negative {
            color: #cc0000;
        }
        .trade-markers {
            margin-top: 20px;
        }
        .buy-marker {
            display: inline-block;
            width: 12px;
            height: 12px;
            background-color: #00aa00;
            border-radius: 50%;
            margin-right: 5px;
        }
        .sell-marker {
            display: inline-block;
            width: 12px;
            height: 12px;
            background-color: #cc0000;
            border-radius: 50%;
            margin-right: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Winter Backtest Results</h1>
            <p>Period: )" << start_date_ << R"( to )" << end_date_ << R"(</p>
        </div>
        
        <div class="chart-container">
            <canvas id="equityChart"></canvas>
        </div>
        
        <div class="trade-markers">
            <p><span class="buy-marker"></span> Buy Trade &nbsp;&nbsp; <span class="sell-marker"></span> Sell Trade</p>
        </div>
        
        <div class="metrics-container">
            <div class="metric-box">
                <div class="metric-title">Initial Capital</div>
                <div class="metric-value">$)" << std::fixed << std::setprecision(2) << metrics.initial_capital << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Final Capital</div>
                <div class="metric-value">$)" << std::fixed << std::setprecision(2) << metrics.final_capital << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Total Return</div>
                <div class="metric-value )" << (metrics.total_return >= 0 ? "positive" : "negative") << R"(">
                    $)" << std::fixed << std::setprecision(2) << metrics.total_return << 
                    " (" << std::setprecision(2) << metrics.total_return_pct << "%)" << R"(
                </div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Annualized Return</div>
                <div class="metric-value )" << (metrics.annualized_return >= 0 ? "positive" : "negative") << R"(">
                    )" << std::fixed << std::setprecision(2) << (metrics.annualized_return * 100) << R"(%
                </div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Sharpe Ratio</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << metrics.sharpe_ratio << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Max Drawdown</div>
                <div class="metric-value negative">
                    $)" << std::fixed << std::setprecision(2) << metrics.max_drawdown << 
                    " (" << std::setprecision(2) << metrics.max_drawdown_pct << "%)" << R"(
                </div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Total Trades</div>
                <div class="metric-value">)" << metrics.total_trades << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Win Rate</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << (metrics.win_rate * 100) << R"(%</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Profit Factor</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << metrics.profit_factor << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Max Consecutive Wins</div>
                <div class="metric-value">)" << metrics.max_consecutive_wins << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Max Consecutive Losses</div>
                <div class="metric-value">)" << metrics.max_consecutive_losses << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Avg Profit Per Trade</div>
                <div class="metric-value positive">$)" << std::fixed << std::setprecision(2) << metrics.avg_profit_per_trade << R"(</div>
            </div>
        </div>
    </div>

    <script>
        const ctx = document.getElementById("equityChart").getContext("2d");
        
        // Buy and sell points
        const buyPoints = )" << buy_points.str() << R"(;
        const sellPoints = )" << sell_points.str() << R"(;
        
        const equityChart = new Chart(ctx, {
            type: "line",
            data: {
                labels: )" << timestamps_json.str() << R"(,
                datasets: [{
                    label: "Equity Curve",
                    data: )" << equity_json.str() << R"(,
                    borderColor: "#0066cc",
                    backgroundColor: 'rgba(0, 102, 204, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.1
                },
                {
                    label: "Buy Points",
                    data: buyPoints,
                    backgroundColor: "#00aa00",
                    borderColor: "#00aa00",
                    pointRadius: 5,
                    pointHoverRadius: 8,
                    showLine: false
                },
                {
                    label: "Sell Points",
                    data: sellPoints,
                    backgroundColor: "#cc0000",
                    borderColor: "#cc0000",
                    pointRadius: 5,
                    pointHoverRadius: 8,
                    showLine: false
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "Equity Curve with Trade Markers"
                    },
                    tooltip: {
                        mode: "index",
                        intersect: false,
                        callbacks: {
                            label: function(context) {
                                if (context.dataset.label === "Equity Curve") {
                                    return "Equity: $" + context.raw.toFixed(2);
                                } else if (context.dataset.label === "Buy Points") {
                                    return "Buy: " + context.raw.symbol + " at $" + context.raw.y.toFixed(2);
                                } else if (context.dataset.label === "Sell Points") {
                                    return "Sell: " + context.raw.symbol + " at $" + context.raw.y.toFixed(2);
                                }
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: false,
                        title: {
                            display: true,
                            text: 'Equity ($)'
                        }
                    },
                    x: {
                        title: {
                            display: true,
                            text: "Time"
                        }
                    }
                }
            }
        });
    </script>
</body>
</html>
)";
    
    html_file.close();
    winter::utils::Logger::info() << "Generated HTML report: " << output_file << winter::utils::Logger::endl;
}

} // namespace winter::backtest
