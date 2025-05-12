#include <winter/core/order.hpp>
#pragma once
#include <string>

namespace winter::core {

Order::Order()
    : side(OrderSide::BUY), quantity(0), price(0.0) {}

Order::Order(const std::string& sym, OrderSide s, int qty, double p)
    : symbol(sym), side(s), quantity(qty), price(p) {}

double Order::total_value() const {
    return price * quantity;
}

} // namespace winter::core
