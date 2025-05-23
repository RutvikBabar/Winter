#include <winter/core/engine.hpp>
#include <winter/strategy/strategy_registry.hpp>
#include <winter/core/market_data.hpp>
#include <winter/utils/flamegraph.hpp>
#include <winter/utils/logger.hpp>
#include "strategies/stat_arbitrage.hpp"
#include <winter/strategy/strategy_factory.hpp>

#include "strategies/mean_reversion_strategy.hpp"  

#include <iostream>
#include <iomanip>
#include <string>
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>
#include <ctime>
#include <fstream>
#include <sstream>
#include <vector>
#include <random>
#include <map>
#include <zmq.hpp>
#include <unordered_map>
#include <deque>
#include <algorithm>
#include <execution>
#include <mutex>
#include <filesystem>
#include <optional>

// ANSI color codes for console output
const std::string RESET = "\033[0m";
const std::string BLUE = "\033[34m";
const std::string GREEN = "\033[32m";
const std::string RED = "\033[31m";
const std::string YELLOW = "\033[33m";
const std::string CYAN = "\033[36m";

// Structure to store trade information for CSV export
struct TradeRecord {
    std::string timestamp;
    std::string symbol;
    std::string side;
    int quantity;
    double price;
    double value;
    double profit_loss;
    double z_score; // Added Z-score to track signal strength
};

// Structure to track position costs
struct PositionTracker {
    int quantity = 0;
    double total_cost = 0.0;
    
    double average_cost() const {
        return quantity > 0 ? total_cost / quantity : 0.0;
    }
    
    void add_position(int qty, double cost) {
        quantity += qty;
        total_cost += cost;
    }
    
    double calculate_profit(int sell_qty, double sell_price) const {
        if (quantity <= 0) return 0.0;
        double avg_cost = average_cost();
        return sell_qty * (sell_price - avg_cost);
    }
    
    void reduce_position(int qty, double& cost_basis) {
        if (quantity <= 0) return;
        
        double avg_cost = average_cost();
        cost_basis = qty * avg_cost;
        
        quantity -= qty;
        total_cost -= cost_basis;
        
        // Ensure we don't have negative values due to rounding
        if (quantity <= 0) {
            quantity = 0;
            total_cost = 0.0;
        }
    }
};

// Global variables
std::atomic<bool> g_running = true;
std::vector<TradeRecord> trade_records;
std::unordered_map<std::string, double> last_z_scores; // Store last Z-score for each symbol
std::unordered_map<std::string, PositionTracker> position_trackers; // Track positions and costs
// Function to parse strategy configuration file
std::unordered_map<std::string, std::string> parse_strategy_config(const std::string& filename) {
    std::unordered_map<std::string, std::string> config_map;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        std::cerr << RED << "Could not open configuration file: " << filename << RESET << std::endl;
        return config_map;
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse key=value or key:value
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            pos = line.find(':');
        }
        
        if (pos != std::string::npos) {
            std::string key = line.substr(0, pos);
            std::string value = line.substr(pos + 1);
            
            // Trim whitespace and quotes
            key.erase(0, key.find_first_not_of(" \t\""));
            key.erase(key.find_last_not_of(" \t\"") + 1);
            value.erase(0, value.find_first_not_of(" \t\""));
            value.erase(value.find_last_not_of(" \t\"") + 1);
            
            config_map[key] = value;
        }
    }
    
    return config_map;
}

void signal_handler(int signal) {
    g_running = false;
    std::cout << "\nReceived interrupt signal. Stopping simulation...\n";
}

// Function to escape CSV field if it contains special characters
std::string escape_csv_field(const std::string& field) {
    if (field.find(',') != std::string::npos || 
        field.find('\"') != std::string::npos || 
        field.find('\n') != std::string::npos) {
        std::string escaped = field;
        // Replace double quotes with two double quotes
        size_t pos = 0;
        while ((pos = escaped.find('\"', pos)) != std::string::npos) {
            escaped.replace(pos, 1, "\"\"");
            pos += 2;
        }
        // Wrap in quotes
        return "\"" + escaped + "\"";
    }
    return field;
}

// Function to export trades to CSV
bool export_trades_to_csv(const std::vector<TradeRecord>& trades, double initial_balance, double final_balance) {
    std::ofstream csv_file("winter_trades.csv");
    if (!csv_file.is_open()) {
        std::cerr << "Error: Could not open CSV file for writing" << std::endl;
        return false;
    }
    
    // Write CSV header
    csv_file << "Time,Symbol,Side,Quantity,Price,Value,P&L,Z-Score" << std::endl;
    
    // Write trade data
    for (const auto& trade : trades) {
        csv_file << escape_csv_field(trade.timestamp) << ","
                 << escape_csv_field(trade.symbol) << ","
                 << escape_csv_field(trade.side) << ","
                 << trade.quantity << ","
                 << std::fixed << std::setprecision(2) << trade.price << ","
                 << std::fixed << std::setprecision(2) << trade.value << ",";
        
        if (trade.side == "SELL") {
            csv_file << std::fixed << std::setprecision(2) << trade.profit_loss;
        }
        csv_file << "," << std::fixed << std::setprecision(4) << trade.z_score;
        csv_file << std::endl;
    }
    
    // Add empty row for separation
    csv_file << std::endl;
    
    // Write summary section
    csv_file << "Summary" << std::endl;
    csv_file << "Initial Balance:," << std::fixed << std::setprecision(2) << initial_balance << std::endl;
    csv_file << "Final Balance:," << std::fixed << std::setprecision(2) << final_balance << std::endl;
    
    double pnl = final_balance - initial_balance;
    csv_file << "P&L:," << std::fixed << std::setprecision(2) << pnl << std::endl;
    
    csv_file.close();
    std::cout << "Trade data exported to winter_trades.csv" << std::endl;
    return true;
}

// Simple JSON parser function for market data
bool parse_json_market_data(const std::string& json_str, winter::core::MarketData& data) {
    try {
        // Find the Symbol field (note the uppercase)
        size_t symbol_pos = json_str.find("\"Symbol\":");
        if (symbol_pos == std::string::npos) return false;
        
        // Extract symbol value
        symbol_pos = json_str.find("\"", symbol_pos + 9) + 1;
        size_t symbol_end = json_str.find("\"", symbol_pos);
        data.symbol = json_str.substr(symbol_pos, symbol_end - symbol_pos);
        
        // Find the Price field (note the uppercase)
        size_t price_pos = json_str.find("\"Price\":");
        if (price_pos == std::string::npos) return false;
        
        // Extract price value
        price_pos += 8; // Move past "Price":
        while (price_pos < json_str.length() && 
              (json_str[price_pos] == ' ' || json_str[price_pos] == '\t' || json_str[price_pos] == '\"')) price_pos++;
        
        size_t price_end = json_str.find_first_of("\",}", price_pos);
        std::string price_str = json_str.substr(price_pos, price_end - price_pos);
        data.price = std::stod(price_str);
        
        // Find the Size field for volume
        size_t size_pos = json_str.find("\"Size\":");
        if (size_pos == std::string::npos) return false;
        
        // Extract size value
        size_pos += 7; // Move past "Size":
        while (size_pos < json_str.length() && 
              (json_str[size_pos] == ' ' || json_str[size_pos] == '\t' || json_str[size_pos] == '\"')) size_pos++;
        
        size_t size_end = json_str.find_first_of("\",}", size_pos);
        std::string size_str = json_str.substr(size_pos, size_end - size_pos);
        data.volume = std::stoi(size_str);
        
        // Set timestamp to current time
        data.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()
        ).count();
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing market data: " << e.what() << std::endl;
        return false;
    }
}

// Function to receive market data from ZMQ socket
winter::core::MarketData receive_market_data(zmq::socket_t& socket) {
    winter::core::MarketData data;
    
    zmq::message_t message;
    // Try to receive a message, non-blocking
    if (socket.recv(message, zmq::recv_flags::dontwait)) {
        // Convert message to string
        std::string json_str(static_cast<char*>(message.data()), message.size());
        
        // Parse JSON using our simple parser (no output to console)
        parse_json_market_data(json_str, data);
    }
    
    return data;
}

// Calculate Z-score for a price series
double calculate_z_score(const std::deque<double>& prices, double current_price) {
    if (prices.size() < 2) return 0.0;
    
    // Calculate mean
    double sum = 0.0;
    for (const auto& price : prices) {
        sum += price;
    }
    double mean = sum / prices.size();
    
    // Calculate standard deviation
    double sum_sq_diff = 0.0;
    for (const auto& price : prices) {
        double diff = price - mean;
        sum_sq_diff += diff * diff;
    }
    double variance = sum_sq_diff / prices.size();
    double std_dev = std::sqrt(std::max(0.0, variance));
    
    // Calculate Z-score
    if (std_dev == 0.0) return 0.0;
    return (current_price - mean) / std_dev;
}

// Run live trading mode
void run_live_trading(const std::string& socket_endpoint, double initial_balance, const std::string& strategy_name) {
    // Setup the engine
    winter::core::Engine engine;
    
    // Load strategies from registry
    auto strategy = winter::strategy::StrategyFactory::create_strategy(strategy_name);
    if (!strategy) {
        std::cout << RED << "Strategy not found: " << strategy_name << RESET << std::endl;
        return;
    }
    

    engine.add_strategy(strategy);
    std::cout << "Using strategy: " << strategy->name() << std::endl;
    

    
    // Initialize portfolio
    engine.portfolio().set_cash(initial_balance);
    
    // Clear trade records for new simulation
    trade_records.clear();
    position_trackers.clear();
    
    // Price history for Z-score calculation
    std::unordered_map<std::string, std::deque<double>> price_history;
    
    // Setup order callback to display trades and record them
    engine.set_order_callback([&](const winter::core::Order& order) {
        auto& portfolio = engine.portfolio();
        double price = order.price;
        int quantity = order.quantity;
        std::string symbol = order.symbol;
        
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        char time_buffer[9];
        std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", tm);
        
        // Get the Z-score that triggered this trade
        double z_score = last_z_scores.count(symbol) ? last_z_scores[symbol] : 0.0;
        
        // Create trade record
        TradeRecord record;
        record.timestamp = time_buffer;
        record.symbol = symbol;
        record.quantity = quantity;
        record.price = price;
        record.value = quantity * price;
        record.z_score = z_score;
        
        if (order.side == winter::core::OrderSide::BUY) {
            // Track position cost for accurate P&L calculation
            if (position_trackers.find(symbol) == position_trackers.end()) {
                position_trackers[symbol] = PositionTracker();
            }
            
            double cost = quantity * price;
            position_trackers[symbol].add_position(quantity, cost);
            
            record.side = "BUY";
            record.profit_loss = 0.0; // No P&L for buys
            
            std::cout << BLUE << "[" << time_buffer << "] BUY " 
                      << quantity << " " << symbol << " @ $" 
                      << std::fixed << std::setprecision(2) << price
                      << " | Z-Score: " << std::fixed << std::setprecision(4) << z_score
                      << " | Balance: $" << portfolio.cash() << RESET << std::endl;
        } else {
            record.side = "SELL";
            
            // Calculate actual profit/loss based on average cost
            double profit = 0.0;
            if (position_trackers.find(symbol) != position_trackers.end()) {
                auto& tracker = position_trackers[symbol];
                
                // Calculate profit based on average cost
                profit = tracker.calculate_profit(quantity, price);
                
                // Reduce the position
                double cost_basis = 0.0;
                tracker.reduce_position(quantity, cost_basis);
            }
            
            record.profit_loss = profit;
            
            if (profit >= 0) {
                std::cout << GREEN << "[" << time_buffer << "] SELL " 
                          << quantity << " " << symbol << " @ $" 
                          << std::fixed << std::setprecision(2) << price
                          << " | Z-Score: " << std::fixed << std::setprecision(4) << z_score
                          << " | Profit: $" << profit
                          << " | Balance: $" << portfolio.cash() << RESET << std::endl;
            } else {
                std::cout << RED << "[" << time_buffer << "] SELL " 
                          << quantity << " " << symbol << " @ $" 
                          << std::fixed << std::setprecision(2) << price
                          << " | Z-Score: " << std::fixed << std::setprecision(4) << z_score
                          << " | Loss: $" << profit
                          << " | Balance: $" << portfolio.cash() << RESET << std::endl;
            }
        }
        
        // Add to trade records
        trade_records.push_back(record);
    });
    
    // Initialize ZMQ
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::sub);
    
    // Connect to the socket
    try {
        std::cout << "Connecting to market data socket at " << socket_endpoint << std::endl;
        socket.connect(socket_endpoint);
        socket.set(zmq::sockopt::subscribe, "");  // Subscribe to all messages
        std::cout << "Connected to market data socket" << std::endl;
    } catch (const zmq::error_t& e) {
        std::cerr << "Failed to connect to market data socket: " << e.what() << std::endl;
        return;
    }
    
    // Start the flamegraph profiling
    winter::utils::Flamegraph flamegraph("winter_profile");
    flamegraph.start();
    
    // Start the engine on specific cores for ultra-low latency
    engine.start(0, 1);  // Pin strategy thread to core 0, execution to core 1
    
    std::cout << CYAN << "Simulation started with $" << initial_balance << RESET << std::endl;
    std::cout << YELLOW << "Press Ctrl+C to stop the simulation" << RESET << std::endl;
    std::cout << "Waiting for market data from socket..." << std::endl;
    
    // Main simulation loop
    auto start_time = std::chrono::high_resolution_clock::now();
    int trade_count = 0;
    int data_count = 0;
    
    while (g_running) {
        // Check if we've run out of money
        if (engine.portfolio().cash() <= 0) {
            std::cout << RED << "Out of funds! Stopping simulation." << RESET << std::endl;
            break;
        }
        
        // Receive market data from socket
        winter::core::MarketData data = receive_market_data(socket);
        
        // Skip empty data (socket might not have data yet)
        if (data.symbol.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        // Update price history for Z-score calculation
        if (price_history.find(data.symbol) == price_history.end()) {
            price_history[data.symbol] = std::deque<double>();
        }
        price_history[data.symbol].push_back(data.price);
        if (price_history[data.symbol].size() > 20) {
            price_history[data.symbol].pop_front();
        }
        
        // Calculate and store Z-score
        double z_score = calculate_z_score(price_history[data.symbol], data.price);
        last_z_scores[data.symbol] = z_score;
        
        // Process market data
        engine.process_market_data(data);
        data_count++;
        
        // Count trades
        trade_count = engine.portfolio().trade_count();
    }
    
    // Stop the engine
    engine.stop();
    
    // Stop flamegraph profiling and generate report
    flamegraph.stop();
    flamegraph.generate_report();
    
    // Print final results
    double final_balance = engine.portfolio().total_value();
    double pnl = final_balance - initial_balance;
    
    std::cout << "\n" << CYAN << "=== Simulation Results ===" << RESET << std::endl;
    std::cout << "Initial Balance: $" << std::fixed << std::setprecision(2) << initial_balance << std::endl;
    std::cout << "Final Balance:   $" << final_balance << std::endl;
    
    if (pnl >= 0) {
        std::cout << GREEN << "Profit:          $" << pnl << " (+" 
                  << (pnl / initial_balance * 100) << "%)" << RESET << std::endl;
    } else {
        std::cout << RED << "Loss:            $" << pnl << " (" 
                  << (pnl / initial_balance * 100) << "%)" << RESET << std::endl;
    }
    
    std::cout << "Total Trades:    " << trade_count << std::endl;
    std::cout << "Data Points:     " << data_count << std::endl;
    
    // Export trades to CSV
    export_trades_to_csv(trade_records, initial_balance, final_balance);
}

// Optimized direct backtesting implementation
void run_backtest(const std::string& csv_file, double initial_balance, const std::string& strategy_name) {
    std::cout << CYAN << "Starting optimized backtest with data from: " << csv_file << RESET << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Load and parse CSV data directly
    std::vector<winter::core::MarketData> historical_data;
    
    // Check if file exists
    if (!std::filesystem::exists(csv_file)) {
        std::cout << RED << "CSV file does not exist: " << csv_file << RESET << std::endl;
        return;
    }
    
    std::ifstream file(csv_file);
    if (!file.is_open()) {
        std::cout << RED << "Failed to open CSV file: " << csv_file << RESET << std::endl;
        return;
    }
    
    std::cout << CYAN << "Reading CSV file..." << RESET << std::endl;
    
    // Read header line
    std::string line;
    std::getline(file, line);
    
    // Read all lines
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();
    
    std::cout << CYAN << "Read " << lines.size() << " lines from CSV file" << RESET << std::endl;
    std::cout << CYAN << "Parsing CSV data in parallel..." << RESET << std::endl;
    
    // Reserve space for data
    historical_data.reserve(lines.size());
    
    // Parse lines in batches to avoid memory issues
    constexpr size_t BATCH_SIZE = 100000;
    for (size_t batch_start = 0; batch_start < lines.size(); batch_start += BATCH_SIZE) {
        size_t batch_end = std::min(batch_start + BATCH_SIZE, lines.size());
        size_t batch_size = batch_end - batch_start;
        
        std::vector<std::optional<winter::core::MarketData>> batch_results(batch_size);
        
        // Parse batch in parallel
        std::transform(
            std::execution::par_unseq,
            lines.begin() + batch_start,
            lines.begin() + batch_end,
            batch_results.begin(),
            [&](const std::string& line) -> std::optional<winter::core::MarketData> {
                std::stringstream ss(line);
                std::string time, symbol, market_center, price_str, size_str;
                std::string cum_bats_vol, cum_sip_vol, sip_complete, last_sale;
                
                // Parse CSV columns
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
                    data.timestamp = 0; // Will be set later
                    
                    return data;
                } catch (const std::exception& e) {
                    return std::nullopt;
                }
            }
        );
        
        // Add valid results to historical_data
        for (auto& result : batch_results) {
            if (result) {
                historical_data.push_back(*result);
            }
        }
        
        // Report progress
        double progress = static_cast<double>(batch_end) / lines.size() * 100.0;
        std::cout << CYAN << "Parsing progress: " << std::fixed << std::setprecision(1) 
                 << progress << "% (" << historical_data.size() << " valid data points)" << RESET << std::endl;
    }
    
    // Set sequential timestamps
    for (size_t i = 0; i < historical_data.size(); i++) {
        historical_data[i].timestamp = i;
    }
    
    // Sort data by timestamp
    std::cout << CYAN << "Sorting data by timestamp..." << RESET << std::endl;
    std::sort(std::execution::par_unseq, historical_data.begin(), historical_data.end(),
             [](const winter::core::MarketData& a, const winter::core::MarketData& b) {
                 return a.timestamp < b.timestamp;
             });
    
    std::cout << CYAN << "Loaded " << historical_data.size() << " data points from " 
             << lines.size() << " total lines in " << csv_file << RESET << std::endl;
    
    // Get strategies from registry
    auto strategy = winter::strategy::StrategyFactory::create_strategy(strategy_name);
    if (!strategy) {
        std::cout << RED << "Strategy not found: " << strategy_name << RESET << std::endl;
        return;
    }
    
    std::cout << "Using strategy: " << strategy->name() << std::endl;
    
    // Initialize portfolio and tracking variables
    double cash = initial_balance;
    std::unordered_map<std::string, PositionTracker> positions;
    std::vector<TradeRecord> trades;
    std::unordered_map<std::string, std::deque<double>> price_history;
    std::unordered_map<std::string, double> last_prices;
    
    // Setup progress reporting
    std::cout << YELLOW << "Running backtest..." << RESET << std::endl;
    
    std::atomic<size_t> processed_count(0);
    std::atomic<bool> running(true);
    
    std::thread progress_thread([&]() {
        while (running && processed_count < historical_data.size()) {
            double progress = static_cast<double>(processed_count) / historical_data.size() * 100.0;
            std::cout << "\rProgress: " << std::fixed << std::setprecision(1) << progress << "%" << std::flush;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        std::cout << "\rProgress: 100.0%" << std::endl;
    });
    
    // Process data sequentially to ensure proper signal generation
    for (size_t i = 0; i < historical_data.size(); i++) {
        const auto& data = historical_data[i];
        
        // Update last known price for each symbol
        last_prices[data.symbol] = data.price;
        
        // Process each data point with all strategies
        
        // Generate signals for this data point
        std::vector<winter::core::Signal> signals = strategy->process_tick(data);
        
        // Process signals
        for (const auto& signal : signals) {
            if (signal.type == winter::core::SignalType::BUY) {
                // Calculate position size (1% of capital)
                double max_position = cash * 0.01;
                int quantity = static_cast<int>(max_position / signal.price);
                
                if (quantity > 0 && cash >= quantity * signal.price) {
                    double cost = quantity * signal.price;
                    
                    // Update cash
                    cash -= cost;
                    
                    // Update position tracker
                    if (positions.find(signal.symbol) == positions.end()) {
                        positions[signal.symbol] = PositionTracker();
                    }
                    positions[signal.symbol].add_position(quantity, cost);
                    
                    // Format timestamp
                    std::string timestamp = std::to_string(data.timestamp);
                    
                    // Record trade
                    TradeRecord record;
                    record.timestamp = timestamp;
                    record.symbol = signal.symbol;
                    record.side = "BUY";
                    record.quantity = quantity;
                    record.price = signal.price;
                    record.value = cost;
                    record.profit_loss = 0.0;
                    record.z_score = last_z_scores.count(signal.symbol) ? last_z_scores[signal.symbol] : 0.0; // Not using z-score for stat arb
                    
                    trades.push_back(record);
                }
            }
            else if (signal.type == winter::core::SignalType::SELL) {
                auto it = positions.find(signal.symbol);
                if (it != positions.end() && it->second.quantity > 0) {
                    int quantity = it->second.quantity;
                    double proceeds = quantity * signal.price;
                    
                    // Calculate profit/loss
                    double profit = it->second.calculate_profit(quantity, signal.price);
                    
                    // Update cash
                    cash += proceeds;
                    
                    // Update position tracker
                    double cost_basis = 0.0;
                    it->second.reduce_position(quantity, cost_basis);
                    
                    // Format timestamp
                    std::string timestamp = std::to_string(data.timestamp);
                    
                    // Record trade
                    TradeRecord record;
                    record.timestamp = timestamp;
                    record.symbol = signal.symbol;
                    record.side = "SELL";
                    record.quantity = quantity;
                    record.price = signal.price;
                    record.value = proceeds;
                    record.profit_loss = profit;
                    record.z_score = last_z_scores.count(signal.symbol) ? last_z_scores[signal.symbol] : 0.0; // Not using z-score for stat arb
                    
                    trades.push_back(record);
                }
            }
        }
    
        
        processed_count++;
    }
    
    // Stop progress thread
    running = false;
    if (progress_thread.joinable()) {
        progress_thread.join();
    }
    
    // Calculate final balance
    double final_balance = cash;
    
    // Add value of any remaining positions
    for (const auto& [symbol, position] : positions) {
        if (position.quantity > 0) {
            // Use last known price for this symbol
            auto it = last_prices.find(symbol);
            if (it != last_prices.end()) {
                final_balance += position.quantity * it->second;
            }
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // Calculate performance metrics
    double total_return = final_balance - initial_balance;
    double total_return_pct = (total_return / initial_balance) * 100.0;
    
    // Count winning and losing trades
    int winning_trades = 0;
    int losing_trades = 0;
    double total_profit = 0.0;
    double total_loss = 0.0;
    
    for (const auto& trade : trades) {
        if (trade.side == "SELL") {
            if (trade.profit_loss > 0) {
                winning_trades++;
                total_profit += trade.profit_loss;
            } else {
                losing_trades++;
                total_loss += std::abs(trade.profit_loss);
            }
        }
    }
    
    double win_rate = (winning_trades + losing_trades > 0) ? 
        static_cast<double>(winning_trades) / (winning_trades + losing_trades) * 100.0 : 0.0;
    
    double profit_factor = (total_loss > 0) ? total_profit / total_loss : 0.0;
    
    // Calculate Sharpe ratio (simplified)
    double sharpe_ratio = 0.0;
    if (trades.size() > 0) {
        std::vector<double> returns;
        double prev_equity = initial_balance;
        double equity = initial_balance;
        
        for (const auto& trade : trades) {
            if (trade.side == "BUY") {
                equity -= trade.value;
            } else {
                equity += trade.value;
            }
            
            // Calculate return for this trade
            double ret = (equity - prev_equity) / prev_equity;
            returns.push_back(ret);
            prev_equity = equity;
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
        
        // Calculate Sharpe ratio (assuming 0 risk-free rate)
        if (std_dev > 0) {
            sharpe_ratio = (avg_return / std_dev) * std::sqrt(252.0); // Annualized
        }
    }
    
    // Calculate max drawdown
    double max_drawdown = 0.0;
    double peak = initial_balance;
    double equity = initial_balance;
    
    for (const auto& trade : trades) {
        if (trade.side == "BUY") {
            equity -= trade.value;
        } else {
            equity += trade.value;
        }
        
        if (equity > peak) {
            peak = equity;
        }
        
        double drawdown = peak - equity;
        if (drawdown > max_drawdown) {
            max_drawdown = drawdown;
        }
    }
    
    double max_drawdown_pct = (max_drawdown / initial_balance) * 100.0;
    
    // Print results
    std::cout << "\n" << CYAN << "=== Backtest Results ===" << RESET << std::endl;
    std::cout << "Initial Capital: $" << std::fixed << std::setprecision(2) << initial_balance << std::endl;
    std::cout << "Final Capital:   $" << std::fixed << std::setprecision(2) << final_balance << std::endl;
    
    if (total_return >= 0) {
        std::cout << GREEN << "Total Return:    $" << std::fixed << std::setprecision(2) << total_return 
                  << " (" << std::setprecision(2) << total_return_pct << "%)" << RESET << std::endl;
    } else {
        std::cout << RED << "Total Return:    $" << std::fixed << std::setprecision(2) << total_return 
                  << " (" << std::setprecision(2) << total_return_pct << "%)" << RESET << std::endl;
    }
    
    std::cout << "Sharpe Ratio:      " << std::fixed << std::setprecision(2) << sharpe_ratio << std::endl;
    std::cout << "Max Drawdown:      " << std::fixed << std::setprecision(2) << max_drawdown_pct << "%" << std::endl;
    std::cout << "Total Trades:      " << trades.size() << std::endl;
    std::cout << "Winning Trades:    " << winning_trades << std::endl;
    std::cout << "Losing Trades:     " << losing_trades << std::endl;
    std::cout << "Win Rate:          " << std::fixed << std::setprecision(2) << win_rate << "%" << std::endl;
    std::cout << "Profit Factor:     " << std::fixed << std::setprecision(2) << profit_factor << std::endl;
    std::cout << "Backtest Duration: " << duration << "ms" << std::endl;
    
    // Generate HTML report with Chart.js
    std::ofstream html_file("backtest_report.html");
    if (html_file.is_open()) {
        // Generate equity curve data
        std::vector<double> equity_curve;
        equity_curve.push_back(initial_balance);
        
        double equity = initial_balance;
        for (const auto& trade : trades) {
            if (trade.side == "BUY") {
                equity -= trade.value;
            } else if (trade.side == "SELL") {
                equity += trade.value;
            }
            equity_curve.push_back(equity);
        }
        
        // Generate JSON for chart
        std::stringstream equity_json;
        equity_json << "[";
        for (size_t i = 0; i < equity_curve.size(); i++) {
            if (i > 0) equity_json << ",";
            equity_json << equity_curve[i];
        }
        equity_json << "]";
        
        std::stringstream labels_json;
        labels_json << "[";
        for (size_t i = 0; i < equity_curve.size(); i++) {
            if (i > 0) labels_json << ",";
            labels_json << "\"" << i << "\"";
        }
        labels_json << "]";
        
        // Write HTML with embedded Chart.js using raw string literals
        html_file << R"(
<!DOCTYPE html>
<html>
<head>
    <title>Winter Backtest Results</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
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
            height: 400px;
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
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Winter Backtest Results</h1>
            <p>Optimized Backtesting Report</p>
        </div>
        
        <div class="chart-container">
            <canvas id="equityChart"></canvas>
        </div>
        
        <div class="metrics-container">
            <div class="metric-box">
                <div class="metric-title">Initial Capital</div>
                <div class="metric-value">$)" << std::fixed << std::setprecision(2) << initial_balance << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Final Capital</div>
                <div class="metric-value">$)" << std::fixed << std::setprecision(2) << final_balance << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Total Return</div>
                <div class="metric-value )" << (total_return >= 0 ? "positive" : "negative") << R"(">
                    $)" << std::fixed << std::setprecision(2) << total_return << 
                    " (" << std::setprecision(2) << total_return_pct << R"(%)
                </div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Sharpe Ratio</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << sharpe_ratio << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Max Drawdown</div>
                <div class="metric-value negative">
                    $)" << std::fixed << std::setprecision(2) << max_drawdown << 
                    " (" << std::setprecision(2) << max_drawdown_pct << R"(%)
                </div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Total Trades</div>
                <div class="metric-value">)" << trades.size() << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Win Rate</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << win_rate << R"(%</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Profit Factor</div>
                <div class="metric-value">)" << std::fixed << std::setprecision(2) << profit_factor << R"(</div>
            </div>
        </div>
    </div>

    <script>
        const ctx = document.getElementById("equityChart").getContext("2d");
        const equityChart = new Chart(ctx, {
            type: "line",
            data: {
                labels: )" << labels_json.str() << R"(,
                datasets: [{
                    label: "Equity Curve",
                    data: )" << equity_json.str() << R"(,
                    borderColor: "#0066cc",
                    backgroundColor: 'rgba(0, 102, 204, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "Equity Curve"
                    },
                    tooltip: {
                        mode: "index",
                        intersect: false,
                        callbacks: {
                            label: function(context) {
                                return "Equity: $" + context.raw.toFixed(2);
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
                            text: "Trade #"
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
        std::cout << GREEN << "Generated HTML report: backtest_report.html" << RESET << std::endl;
    }
    
    // Export trades to CSV
    export_trades_to_csv(trades, initial_balance, final_balance);
}

void generate_trade_graphs(const std::vector<TradeRecord>& trades, double initial_balance, double final_balance);


void run_trade_simulation(const std::string& csv_file, double initial_balance, const std::string& strategy_name) {
    std::cout << CYAN << "Starting trade simulation with data from: " << csv_file << RESET << std::endl;
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    // Load and parse CSV data directly
    std::vector<winter::core::MarketData> historical_data;
    
    // Check if file exists
    if (!std::filesystem::exists(csv_file)) {
        std::cout << RED << "CSV file does not exist: " << csv_file << RESET << std::endl;
        return;
    }
    
    std::ifstream file(csv_file);
    if (!file.is_open()) {
        std::cout << RED << "Failed to open CSV file: " << csv_file << RESET << std::endl;
        return;
    }
    
    std::cout << CYAN << "Reading CSV file..." << RESET << std::endl;
    
    // Read header line
    std::string line;
    std::getline(file, line);
    
    // Read all lines
    std::vector<std::string> lines;
    while (std::getline(file, line)) {
        lines.push_back(line);
    }
    file.close();
    
    std::cout << CYAN << "Read " << lines.size() << " lines from CSV file" << RESET << std::endl;
    std::cout << CYAN << "Parsing CSV data in parallel..." << RESET << std::endl;
    
    // Reserve space for data
    historical_data.reserve(lines.size());
    
    // Parse lines in batches to avoid memory issues
    constexpr size_t BATCH_SIZE = 100000;
    for (size_t batch_start = 0; batch_start < lines.size(); batch_start += BATCH_SIZE) {
        size_t batch_end = std::min(batch_start + BATCH_SIZE, lines.size());
        size_t batch_size = batch_end - batch_start;
        
        std::vector<std::optional<winter::core::MarketData>> batch_results(batch_size);
        
        // Parse batch in parallel
        std::transform(
            std::execution::par_unseq,
            lines.begin() + batch_start,
            lines.begin() + batch_end,
            batch_results.begin(),
            [&](const std::string& line) -> std::optional<winter::core::MarketData> {
                std::stringstream ss(line);
                std::string time, symbol, market_center, price_str, size_str;
                std::string cum_bats_vol, cum_sip_vol, sip_complete, last_sale;
                
                // Parse CSV columns
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
                    data.timestamp = batch_start + (&line - &(*lines.begin())); // Simple sequential timestamp
                    
                    return data;
                } catch (const std::exception& e) {
                    return std::nullopt;
                }
            }
        );
        
        // Add valid results to historical_data
        for (auto& result : batch_results) {
            if (result) {
                historical_data.push_back(*result);
            }
        }
        
        // Report progress
        double progress = static_cast<double>(batch_end) / lines.size() * 100.0;
        std::cout << CYAN << "Parsing progress: " << std::fixed << std::setprecision(1) 
                 << progress << "% (" << historical_data.size() << " valid data points)" << RESET << std::endl;
    }
    
    // Sort data by timestamp
    std::cout << CYAN << "Sorting data by timestamp..." << RESET << std::endl;
    std::sort(std::execution::par_unseq, historical_data.begin(), historical_data.end(),
             [](const winter::core::MarketData& a, const winter::core::MarketData& b) {
                 return a.timestamp < b.timestamp;
             });
    
    std::cout << CYAN << "Loaded " << historical_data.size() << " data points from " 
              << lines.size() << " total lines in " << csv_file << RESET << std::endl;
    
    // Group data by symbol for parallel processing
    std::cout << CYAN << "Grouping data by symbol for parallel processing..." << RESET << std::endl;
    std::unordered_map<std::string, std::vector<winter::core::MarketData>> symbol_data;
    for (const auto& data : historical_data) {
        symbol_data[data.symbol].push_back(data);
    }
    std::cout << CYAN << "Found " << symbol_data.size() << " unique symbols" << RESET << std::endl;
    
    // Setup the engine
    winter::core::Engine engine;
    
    // Get strategies from registry
    auto strategy = winter::strategy::StrategyFactory::create_strategy(strategy_name);
    if (!strategy) {
        std::cout << RED << "Strategy not found: " << strategy_name << RESET << std::endl;
        return;
    }
    

    engine.add_strategy(strategy);
    std::cout << "Using strategy: " << strategy->name() << std::endl;
    

    
    // Initialize portfolio
    engine.portfolio().set_cash(initial_balance);
    
    // Clear trade records for new simulation
    trade_records.clear();
    position_trackers.clear();
    
    // Setup order callback to record trades
    engine.set_order_callback([&](const winter::core::Order& order) {
        auto& portfolio = engine.portfolio();
        double price = order.price;
        int quantity = order.quantity;
        std::string symbol = order.symbol;
        
        // Format timestamp
        std::time_t now = std::time(nullptr);
        std::tm* tm = std::localtime(&now);
        char time_buffer[20];
        std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", tm);
        
        // Get the Z-score that triggered this trade
        double z_score = last_z_scores.count(symbol) ? last_z_scores[symbol] : 0.0;
        
        // Create trade record
        TradeRecord record;
        record.timestamp = time_buffer;
        record.symbol = symbol;
        record.quantity = quantity;
        record.price = price;
        record.value = quantity * price;
        record.z_score = z_score;
        
        if (order.side == winter::core::OrderSide::BUY) {
            // Track position cost for accurate P&L calculation
            if (position_trackers.find(symbol) == position_trackers.end()) {
                position_trackers[symbol] = PositionTracker();
            }
            
            double cost = quantity * price;
            position_trackers[symbol].add_position(quantity, cost);
            
            record.side = "BUY";
            record.profit_loss = 0.0; // No P&L for buys
        } else {
            record.side = "SELL";
            
            // Calculate actual profit/loss based on average cost
            double profit = 0.0;
            if (position_trackers.find(symbol) != position_trackers.end()) {
                auto& tracker = position_trackers[symbol];
                
                // Calculate profit based on average cost
                profit = tracker.calculate_profit(quantity, price);
                
                // Reduce the position
                double cost_basis = 0.0;
                tracker.reduce_position(quantity, cost_basis);
            }
            
            record.profit_loss = profit;
        }
        
        // Add to trade records
        trade_records.push_back(record);
    });
    
    // Start the engine
    engine.start();
    
    // Setup progress reporting
    std::cout << YELLOW << "Running trade simulation..." << RESET << std::endl;
    
    std::atomic<size_t> processed_count(0);
    std::atomic<bool> running(true);
    
    std::thread progress_thread([&]() {
        while (running && processed_count < historical_data.size()) {
            double progress = static_cast<double>(processed_count) / historical_data.size() * 100.0;
            std::cout << "\rProgress: " << std::fixed << std::setprecision(1) << progress << "%" << std::flush;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "\rProgress: 100.0%" << std::endl;
    });
    
    // Process historical data in parallel by symbol
    // This is the key parallelization improvement
    const int NUM_THREADS = std::thread::hardware_concurrency();
    std::cout << CYAN << "Using " << NUM_THREADS << " parallel threads for processing" << RESET << std::endl;
    
    std::vector<std::thread> processing_threads;
    std::mutex engine_mutex; // To protect engine.process_market_data
    
    // Split symbols into groups for each thread
    std::vector<std::vector<std::string>> symbol_groups(NUM_THREADS);
    {
        std::vector<std::string> all_symbols;
        for (const auto& [symbol, _] : symbol_data) {
            all_symbols.push_back(symbol);
        }
        
        for (size_t i = 0; i < all_symbols.size(); ++i) {
            symbol_groups[i % NUM_THREADS].push_back(all_symbols[i]);
        }
    }
    
    // Create threads for processing each group
    for (int thread_id = 0; thread_id < NUM_THREADS; ++thread_id) {
        processing_threads.emplace_back([&, thread_id]() {
            // Get symbols for this thread
            const auto& symbols = symbol_groups[thread_id];
            
            // Process each symbol's data
            for (const auto& symbol : symbols) {
                const auto& data_points = symbol_data[symbol];
                
                for (const auto& data : data_points) {
                    // Process market data with a lock to prevent race conditions
                    {
                        std::lock_guard<std::mutex> lock(engine_mutex);
                        engine.process_market_data(data);
                    }
                    
                    // Increment processed count
                    processed_count.fetch_add(1);
                    
                    // Add small delay to prevent overwhelming the engine
                    if (thread_id > 0) { // Only add delay for non-primary threads
                        std::this_thread::sleep_for(std::chrono::microseconds(10));
                    }
                }
            }
        });
    }
    
    // Wait for all processing threads to complete
    for (auto& thread : processing_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    // Stop progress thread
    running = false;
    if (progress_thread.joinable()) {
        progress_thread.join();
    }
    
    // Stop the engine
    engine.stop();
    
    // Calculate final results
    double final_balance = engine.portfolio().total_value();
    double pnl = final_balance - initial_balance;
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    
    // Print results
    std::cout << "\n" << CYAN << "=== Trade Simulation Results ===" << RESET << std::endl;
    std::cout << "Initial Balance: $" << std::fixed << std::setprecision(2) << initial_balance << std::endl;
    std::cout << "Final Balance:   $" << std::fixed << std::setprecision(2) << final_balance << std::endl;
    
    if (pnl >= 0) {
        std::cout << GREEN << "Profit:          $" << pnl << " (+" 
                  << (pnl / initial_balance * 100) << "%)" << RESET << std::endl;
    } else {
        std::cout << RED << "Loss:            $" << pnl << " (" 
                  << (pnl / initial_balance * 100) << "%)" << RESET << std::endl;
    }
    
    std::cout << "Total Trades:    " << trade_records.size() << std::endl;
    std::cout << "Data Points:     " << historical_data.size() << std::endl;
    std::cout << "Simulation Duration: " << duration << "ms" << std::endl;
    
    // Generate detailed trade graphs
    generate_trade_graphs(trade_records, initial_balance, final_balance);
    
    // Export trades to CSV
    export_trades_to_csv(trade_records, initial_balance, final_balance);
}


// Add this function to generate detailed trade graphs
void generate_trade_graphs(const std::vector<TradeRecord>& trades, double initial_balance, double final_balance) {
    std::ofstream html_file("trade_result_graphs.html");
    if (!html_file.is_open()) {
        std::cout << RED << "Failed to create trade result graphs" << RESET << std::endl;
        return;
    }
    
    // Generate equity curve data
    std::vector<double> equity_curve;
    equity_curve.push_back(initial_balance);
    
    // Generate trade P&L data
    std::vector<double> trade_pnl;
    std::vector<std::string> trade_symbols;
    std::vector<std::string> trade_timestamps;
    
    // Generate cumulative P&L by symbol
    std::unordered_map<std::string, double> symbol_pnl;
    std::unordered_map<std::string, int> symbol_trade_count;
    
    // Generate z-score data
    std::vector<double> z_scores;
    
    double equity = initial_balance;
    for (const auto& trade : trades) {
        if (trade.side == "BUY") {
            equity -= trade.value;
        } else if (trade.side == "SELL") {
            equity += trade.value;
            
            // Track P&L by trade
            trade_pnl.push_back(trade.profit_loss);
            trade_symbols.push_back(trade.symbol);
            trade_timestamps.push_back(trade.timestamp);
            
            // Track P&L by symbol
            symbol_pnl[trade.symbol] += trade.profit_loss;
            symbol_trade_count[trade.symbol]++;
        }
        
        equity_curve.push_back(equity);
        z_scores.push_back(trade.z_score);
    }
    
    // Generate JSON for charts
    std::stringstream equity_json;
    equity_json << "[";
    for (size_t i = 0; i < equity_curve.size(); i++) {
        if (i > 0) equity_json << ",";
        equity_json << equity_curve[i];
    }
    equity_json << "]";
    
    std::stringstream labels_json;
    labels_json << "[";
    for (size_t i = 0; i < equity_curve.size(); i++) {
        if (i > 0) labels_json << ",";
        labels_json << "\"" << i << "\"";
    }
    labels_json << "]";
    
    std::stringstream pnl_json;
    pnl_json << "[";
    for (size_t i = 0; i < trade_pnl.size(); i++) {
        if (i > 0) pnl_json << ",";
        pnl_json << trade_pnl[i];
    }
    pnl_json << "]";
    
    std::stringstream symbol_json;
    symbol_json << "[";
    for (size_t i = 0; i < trade_symbols.size(); i++) {
        if (i > 0) symbol_json << ",";
        symbol_json << "\"" << trade_symbols[i] << "\"";
    }
    symbol_json << "]";
    
    std::stringstream timestamp_json;
    timestamp_json << "[";
    for (size_t i = 0; i < trade_timestamps.size(); i++) {
        if (i > 0) timestamp_json << ",";
        timestamp_json << "\"" << trade_timestamps[i] << "\"";
    }
    timestamp_json << "]";
    
    std::stringstream z_score_json;
    z_score_json << "[";
    for (size_t i = 0; i < z_scores.size(); i++) {
        if (i > 0) z_score_json << ",";
        z_score_json << z_scores[i];
    }
    z_score_json << "]";
    
    // Generate symbol P&L data for bar chart
    std::vector<std::string> symbol_names;
    std::vector<double> symbol_profits;
    std::vector<int> symbol_counts;
    
    for (const auto& [symbol, pnl] : symbol_pnl) {
        symbol_names.push_back(symbol);
        symbol_profits.push_back(pnl);
        symbol_counts.push_back(symbol_trade_count[symbol]);
    }
    
    std::stringstream symbol_names_json;
    symbol_names_json << "[";
    for (size_t i = 0; i < symbol_names.size(); i++) {
        if (i > 0) symbol_names_json << ",";
        symbol_names_json << "\"" << symbol_names[i] << "\"";
    }
    symbol_names_json << "]";
    
    std::stringstream symbol_profits_json;
    symbol_profits_json << "[";
    for (size_t i = 0; i < symbol_profits.size(); i++) {
        if (i > 0) symbol_profits_json << ",";
        symbol_profits_json << symbol_profits[i];
    }
    symbol_profits_json << "]";
    
    std::stringstream symbol_counts_json;
    symbol_counts_json << "[";
    for (size_t i = 0; i < symbol_counts.size(); i++) {
        if (i > 0) symbol_counts_json << ",";
        symbol_counts_json << symbol_counts[i];
    }
    symbol_counts_json << "]";
    
    // Write HTML with embedded Chart.js
    html_file << R"(
<!DOCTYPE html>
<html>
<head>
    <title>Winter Trade Simulation Results</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
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
            height: 400px;
            margin-bottom: 30px;
        }
        .metrics-container {
            display: flex;
            flex-wrap: wrap;
            justify-content: space-between;
            margin-bottom: 30px;
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
        .chart-row {
            display: flex;
            margin-bottom: 30px;
        }
        .chart-col {
            flex: 1;
            padding: 10px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Winter Trade Simulation Results</h1>
            <p>Real Market Simulation Report</p>
        </div>
        
        <div class="metrics-container">
            <div class="metric-box">
                <div class="metric-title">Initial Capital</div>
                <div class="metric-value">$)" << std::fixed << std::setprecision(2) << initial_balance << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Final Capital</div>
                <div class="metric-value">$)" << std::fixed << std::setprecision(2) << final_balance << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Total Return</div>
                <div class="metric-value )" << (final_balance > initial_balance ? "positive" : "negative") << R"(">
                    $)" << std::fixed << std::setprecision(2) << (final_balance - initial_balance) << 
                    " (" << std::setprecision(2) << ((final_balance - initial_balance) / initial_balance * 100) << R"(%)
                </div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Total Trades</div>
                <div class="metric-value">)" << trades.size() << R"(</div>
            </div>
            <div class="metric-box">
                <div class="metric-title">Symbols Traded</div>
                <div class="metric-value">)" << symbol_names.size() << R"(</div>
            </div>
        </div>
        
        <div class="chart-container">
            <canvas id="equityChart"></canvas>
        </div>
        
        <div class="chart-row">
            <div class="chart-col">
                <div class="chart-container">
                    <canvas id="pnlChart"></canvas>
                </div>
            </div>
            <div class="chart-col">
                <div class="chart-container">
                    <canvas id="zScoreChart"></canvas>
                </div>
            </div>
        </div>
        
        <div class="chart-row">
            <div class="chart-col">
                <div class="chart-container">
                    <canvas id="symbolPnlChart"></canvas>
                </div>
            </div>
            <div class="chart-col">
                <div class="chart-container">
                    <canvas id="symbolCountChart"></canvas>
                </div>
            </div>
        </div>
    </div>

    <script>
        // Equity Curve Chart
        const ctxEquity = document.getElementById("equityChart").getContext("2d");
        const equityChart = new Chart(ctxEquity, {
            type: "line",
            data: {
                labels: )" << labels_json.str() << R"(,
                datasets: [{
                    label: "Equity Curve",
                    data: )" << equity_json.str() << R"(,
                    borderColor: "#0066cc",
                    backgroundColor: 'rgba(0, 102, 204, 0.1)',
                    borderWidth: 2,
                    fill: true,
                    tension: 0.1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "Equity Curve"
                    },
                    tooltip: {
                        mode: "index",
                        intersect: false,
                        callbacks: {
                            label: function(context) {
                                return "Equity: $" + context.raw.toFixed(2);
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
                            text: "Trade #"
                        }
                    }
                }
            }
        });
        
        // Trade P&L Chart
        const ctxPnl = document.getElementById("pnlChart").getContext("2d");
        const pnlChart = new Chart(ctxPnl, {
            type: "bar",
            data: {
                labels: )" << timestamp_json.str() << R"(,
                datasets: [{
                    label: "Trade P&L",
                    data: )" << pnl_json.str() << R"(,
                    backgroundColor: function(context) {
                        const value = context.dataset.data[context.dataIndex];
                        return value >= 0 ? 'rgba(0, 170, 0, 0.7)' : 'rgba(204, 0, 0, 0.7)';
                    },
                    borderColor: function(context) {
                        const value = context.dataset.data[context.dataIndex];
                        return value >= 0 ? 'rgba(0, 170, 0, 1)' : 'rgba(204, 0, 0, 1)';
                    },
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "Trade P&L"
                    },
                    tooltip: {
                        callbacks: {
                            title: function(context) {
                                return context[0].label;
                            },
                            label: function(context) {
                                const symbol = )" << symbol_json.str() << R"([context.dataIndex];
                                const value = context.raw.toFixed(2);
                                return symbol + ": $" + value;
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        title: {
                            display: true,
                            text: 'P&L ($)'
                        }
                    },
                    x: {
                        display: false
                    }
                }
            }
        });
        
        // Z-Score Chart
        const ctxZScore = document.getElementById("zScoreChart").getContext("2d");
        const zScoreChart = new Chart(ctxZScore, {
            type: "line",
            data: {
                labels: )" << timestamp_json.str() << R"(,
                datasets: [{
                    label: "Z-Score",
                    data: )" << z_score_json.str() << R"(,
                    borderColor: "#9900cc",
                    backgroundColor: 'rgba(153, 0, 204, 0.1)',
                    borderWidth: 2,
                    fill: false,
                    pointRadius: 3
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "Z-Score at Trade Time"
                    },
                    tooltip: {
                        callbacks: {
                            title: function(context) {
                                return context[0].label;
                            },
                            label: function(context) {
                                const symbol = )" << symbol_json.str() << R"([context.dataIndex];
                                const value = context.raw.toFixed(4);
                                return symbol + ": Z-Score " + value;
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        title: {
                            display: true,
                            text: 'Z-Score'
                        }
                    },
                    x: {
                        display: false
                    }
                }
            }
        });
        
        // Symbol P&L Chart
        const ctxSymbolPnl = document.getElementById("symbolPnlChart").getContext("2d");
        const symbolPnlChart = new Chart(ctxSymbolPnl, {
            type: "bar",
            data: {
                labels: )" << symbol_names_json.str() << R"(,
                datasets: [{
                    label: "P&L by Symbol",
                    data: )" << symbol_profits_json.str() << R"(,
                    backgroundColor: function(context) {
                        const value = context.dataset.data[context.dataIndex];
                        return value >= 0 ? 'rgba(0, 170, 0, 0.7)' : 'rgba(204, 0, 0, 0.7)';
                    },
                    borderColor: function(context) {
                        const value = context.dataset.data[context.dataIndex];
                        return value >= 0 ? 'rgba(0, 170, 0, 1)' : 'rgba(204, 0, 0, 1)';
                    },
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "P&L by Symbol"
                    },
                    tooltip: {
                        callbacks: {
                            label: function(context) {
                                return "P&L: $" + context.raw.toFixed(2);
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        title: {
                            display: true,
                            text: 'P&L ($)'
                        }
                    },
                    x: {
                        title: {
                            display: true,
                            text: "Symbol"
                        }
                    }
                }
            }
        });
        
        // Symbol Count Chart
        const ctxSymbolCount = document.getElementById("symbolCountChart").getContext("2d");
        const symbolCountChart = new Chart(ctxSymbolCount, {
            type: "bar",
            data: {
                labels: )" << symbol_names_json.str() << R"(,
                datasets: [{
                    label: "Trades by Symbol",
                    data: )" << symbol_counts_json.str() << R"(,
                    backgroundColor: 'rgba(255, 159, 64, 0.7)',
                    borderColor: 'rgba(255, 159, 64, 1)',
                    borderWidth: 1
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: {
                    title: {
                        display: true,
                        text: "Trades by Symbol"
                    },
                    tooltip: {
                        callbacks: {
                            label: function(context) {
                                return "Trades: " + context.raw;
                            }
                        }
                    }
                },
                scales: {
                    y: {
                        beginAtZero: true,
                        title: {
                            display: true,
                            text: 'Number of Trades'
                        }
                    },
                    x: {
                        title: {
                            display: true,
                            text: "Symbol"
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
    std::cout << GREEN << "Generated trade result graphs: trade_result_graphs.html" << RESET << std::endl;
}



int main(int argc, char* argv[]) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);
    
    // Parse command line arguments
    std::string socket_endpoint = "tcp://127.0.0.1:5555";
    double initial_balance = 5000000.0;
    bool backtest_mode = false;
    bool trade_mode = false;
    std::string csv_file;
    std::string strategy_id = "1"; // Default to strategy 1
    std::string config_file = "winter_strategies.conf"; // Default config file
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket-endpoint" && i + 1 < argc) {
            socket_endpoint = argv[++i];
        } else if (arg == "--initial-balance" && i + 1 < argc) {
            initial_balance = std::stod(argv[++i]);
        } else if (arg == "--backtest" && i + 1 < argc) {
            backtest_mode = true;
            
            // Check if the next argument is a strategy ID (number)
            if (isdigit(argv[i+1][0])) {
                strategy_id = argv[++i];
                
                // Check if there's another argument for the data file
                if (i + 1 < argc && argv[i+1][0] != '-') {
                    csv_file = argv[++i];
                }
            } else {
                csv_file = argv[++i];
            }
        } else if (arg == "--trade" && i + 1 < argc) {
            trade_mode = true;
            
            // Check if the next argument is a strategy ID (number)
            if (isdigit(argv[i+1][0])) {
                strategy_id = argv[++i];
                
                // Check if there's another argument for the data file
                if (i + 1 < argc && argv[i+1][0] != '-') {
                    csv_file = argv[++i];
                }
            } else {
                csv_file = argv[++i];
            }
        } else if (arg == "--config" && i + 1 < argc) {
            config_file = argv[++i];
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --socket-endpoint <endpoint>  ZMQ socket endpoint (default: tcp://127.0.0.1:5555)" << std::endl;
            std::cout << "  --initial-balance <amount>    Initial balance (default: 5000000.0)" << std::endl;
            std::cout << "  --backtest <csv_file>         Run in backtest mode using historical data from CSV" << std::endl;
            std::cout << "  --trade <strategy_id> <csv_file>  Run trade simulation with specified strategy on market data from CSV" << std::endl;
            std::cout << "  --config <config_file>        Strategy configuration file (default: winter_strategies.conf)" << std::endl;
            std::cout << "  --help                        Show this help message" << std::endl;
            return 0;
        }
    }
    
    try {
        // Load strategy configuration
        auto config_map = parse_strategy_config(config_file);
        
        // Get strategy name from ID
        std::string strategy_name;
        auto it = config_map.find(strategy_id);
        if (it != config_map.end()) {
            strategy_name = it->second;
            std::cout << "Selected strategy: " << strategy_name << std::endl;
        } else {
            std::cout << RED << "Strategy ID " << strategy_id << " not found in configuration." << RESET << std::endl;
            return 1;
        }
        
        // Run in appropriate mode
        if (backtest_mode) {
            run_backtest(csv_file, initial_balance, strategy_name);
        } else if (trade_mode) {
            run_trade_simulation(csv_file, initial_balance, strategy_name);
        } else {
            run_live_trading(socket_endpoint, initial_balance, strategy_name);
        }
    } catch (const std::exception& e) {
        std::cout << RED << "Error: " << e.what() << RESET << std::endl;
        return 1;
    }
    
    return 0;
}
