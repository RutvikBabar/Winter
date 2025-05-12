#include <winter/core/engine.hpp>
#include <winter/strategy/strategy_registry.hpp>
#include <winter/core/market_data.hpp>
#include <winter/utils/flamegraph.hpp>
#include <winter/utils/logger.hpp>
#include "strategies/mean_reversion_strategy.hpp" // Your strategy implementation

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

// Signal handler for strategy signals
void handle_strategy_signal(const winter::core::Signal& signal, std::unordered_map<std::string, std::deque<double>>& price_history) {
    // Update price history
    if (price_history.find(signal.symbol) == price_history.end()) {
        price_history[signal.symbol] = std::deque<double>();
    }
    
    auto& prices = price_history[signal.symbol];
    prices.push_back(signal.price);
    
    // Keep only the last 20 prices
    if (prices.size() > 20) {
        prices.pop_front();
    }
    
    // Calculate Z-score
    double z_score = calculate_z_score(prices, signal.price);
    
    // Store Z-score for later use
    last_z_scores[signal.symbol] = z_score;
}

int main(int argc, char* argv[]) {
    // Register signal handler for Ctrl+C
    std::signal(SIGINT, signal_handler);
    
    // Register the strategy
    auto strategy = winter::strategy::StrategyRegistry::create_and_register<MeanReversionStrategy>();

    // Parse command line arguments
    std::string socket_endpoint = "tcp://127.0.0.1:5555";
    double initial_balance = 100000.0;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--socket-endpoint" && i + 1 < argc) {
            socket_endpoint = argv[++i];
        } else if (arg == "--initial-balance" && i + 1 < argc) {
            initial_balance = std::stod(argv[++i]);
        }
    }
    
    // Setup the engine
    winter::core::Engine engine;
    
    // Load strategies from registry
    auto strategies = winter::strategy::StrategyRegistry::get_all_strategies();
    if (strategies.empty()) {
        std::cout << "No strategies found in registry. Please register strategies before running simulation." << std::endl;
        return 1;
    }
    
    for (const auto& strategy : strategies) {
        engine.add_strategy(strategy);
        std::cout << "Loaded strategy: " << strategy->name() << std::endl;
    }
    
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
        return 1;
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
    auto start_time = std::chrono::system_clock::now();
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
    
    return 0;
}
