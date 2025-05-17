#pragma once
#include <string>
#include <cstdint>

namespace winter::core {

struct MarketData {
    std::string symbol;
    double price;
    int volume;
    int64_t timestamp; // microseconds since epoch
    
    MarketData();

    MarketData(const std::string& sym, double p, int vol);
};

} // namespace winter::core
