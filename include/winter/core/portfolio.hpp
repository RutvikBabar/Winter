#pragma once
#include <string>
#include <unordered_map>
#include <vector>

namespace winter::core {

struct Position {
    int quantity;  // Not atomic
    double cost;   // Not atomic
};

// Add this struct to track trades
struct Trade {
    std::string symbol;
    std::string side;
    int quantity;
    double price;
    double cost;
    double profit;
    std::string timestamp;
};

class Portfolio {
private:
    double cash_;
    std::unordered_map<std::string, Position> positions_;
    int trade_count_;
    std::vector<Trade> trades_;  // Add this member variable
    
public:
    Portfolio();
    
    void set_cash(double amount);
    double cash() const;
    void add_cash(double amount);
    void reduce_cash(double amount);
    
    int get_position(const std::string& symbol) const;
    double get_position_cost(const std::string& symbol) const;
    void add_position(const std::string& symbol, int quantity, double cost);
    void reduce_position(const std::string& symbol, int quantity);
    
    double total_value() const;
    int trade_count() const;
    
    // Add this method to access trades
    const std::vector<Trade>& get_trades() const {
        return trades_;
    }
};

} // namespace winter::core
