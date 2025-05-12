#include <winter/core/market_data.hpp>
#include <chrono>

namespace winter::core {

MarketData::MarketData()
    : price(0.0), volume(0), timestamp(0) {}

MarketData::MarketData(const std::string& sym, double p, int vol)
    : symbol(sym), price(p), volume(vol) {
    // Set timestamp to current time
    timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
}

} // namespace winter::core
