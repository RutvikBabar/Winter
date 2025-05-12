#pragma once

#include <string>

namespace winter::core {

enum class OrderSide {
    BUY,
    SELL
};

enum class OrderType {
    MARKET,
    LIMIT
};

struct Order {
    std::string symbol;
    OrderSide side;
    OrderType type;
    int quantity;
    double price;
    
    Order();
    Order(const std::string& sym, OrderSide s, int qty, double p);
    
    double total_value() const;
};

} // namespace winter::core
