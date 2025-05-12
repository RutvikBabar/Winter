#pragma once
#include <winter/strategy/strategy_base.hpp>
#include <winter/core/market_data.hpp>
#include <winter/core/order.hpp>
#include <winter/core/portfolio.hpp>
#include <winter/utils/lock_free_queue.hpp>
#include <winter/utils/memory_pool.hpp>
#include <winter/utils/logger.hpp>
#include <atomic>
#include <thread>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>

namespace winter::core {

class Engine {
private:
    std::vector<strategy::StrategyPtr> strategies_;
    Portfolio portfolio_;
    utils::MemoryPool<MarketData, 1024> market_data_pool_;
    utils::LockFreeQueue<MarketData*, 4096> market_data_queue_;
    utils::LockFreeQueue<Order, 1024> order_queue_;
    
    std::atomic<bool> running_{false};
    std::unique_ptr<std::thread> strategy_thread_;
    std::unique_ptr<std::thread> execution_thread_;
    
    std::function<void(const Order&)> order_callback_;
    
    void strategy_loop();
    void execution_loop();
    
public:
    Engine();
    ~Engine();
    
    void add_strategy(strategy::StrategyPtr strategy);
    void process_market_data(const MarketData& data);
    void start(int strategy_core_id = -1, int execution_core_id = -1);
    void stop();
    
    void set_order_callback(std::function<void(const Order&)> callback) {
        order_callback_ = std::move(callback);
    }
    
    const Portfolio& portfolio() const { return portfolio_; }
    Portfolio& portfolio() { return portfolio_; }
};

} // namespace winter::core
