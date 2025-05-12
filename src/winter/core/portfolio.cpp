#include <winter/core/portfolio.hpp>
#include <winter/utils/logger.hpp>
#include <algorithm>

namespace winter::core {

Portfolio::Portfolio() : cash_(0.0), trade_count_(0) {}

void Portfolio::set_cash(double amount) {
    cash_ = amount;
}

double Portfolio::cash() const {
    return cash_;
}

void Portfolio::add_cash(double amount) {
    cash_ += amount;
}

void Portfolio::reduce_cash(double amount) {
    cash_ -= amount;
    if (cash_ < 0) {
        utils::Logger::warn() << "Portfolio cash balance negative: " << cash_ << utils::Logger::endl;
    }
}

int Portfolio::get_position(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        return it->second.quantity;
    }
    return 0;
}

double Portfolio::get_position_cost(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        return it->second.cost;
    }
    return 0.0;
}

void Portfolio::add_position(const std::string& symbol, int quantity, double cost) {
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        // Update existing position
        it->second.quantity += quantity;
        it->second.cost += cost;
    } else {
        // Create new position
        Position pos;
        pos.quantity = quantity;
        pos.cost = cost;
        positions_[symbol] = pos;
    }
    
    // Increment trade count
    trade_count_++;
}

void Portfolio::reduce_position(const std::string& symbol, int quantity) {
    auto it = positions_.find(symbol);
    if (it != positions_.end()) {
        // Calculate proportion of position being sold
        double proportion = static_cast<double>(quantity) / it->second.quantity;
        
        // Update position
        it->second.quantity -= quantity;
        it->second.cost -= (it->second.cost * proportion);
        
        // Remove position if quantity is zero
        if (it->second.quantity <= 0) {
            positions_.erase(it);
        }
        
        // Increment trade count
        trade_count_++;
    }
}

double Portfolio::total_value() const {
    // Sum up value of all positions plus cash
    double total = cash_;
    for (const auto& [symbol, position] : positions_) {
        // Note: This is a simplified calculation that doesn't account for current market prices
        // In a real system, you would need to use current market prices to value positions
        total += position.cost;
    }
    return total;
}

int Portfolio::trade_count() const {
    return trade_count_;
}

} // namespace winter::core
