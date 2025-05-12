#pragma once
#include <string>

namespace winter::core {

enum class SignalType {
    BUY,
    SELL,
    EXIT,
    NEUTRAL
};

struct Signal {
    std::string symbol;
    SignalType type;
    double strength; // 0.0 to 1.0
    double price;
    
    Signal();
    Signal(const std::string& sym, SignalType t, double s, double p);
};

} // namespace winter::core
