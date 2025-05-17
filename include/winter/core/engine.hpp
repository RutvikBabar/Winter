#pragma once

#include <winter/strategy/strategy_base.hpp>
#include <winter/core/portfolio.hpp>
#include <winter/core/market_data.hpp>
#include <winter/core/order.hpp>
#include <winter/utils/lock_free_queue.hpp>
#include <winter/utils/core_affinity.hpp>

#include <vector>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>

namespace winter::core {

// Engine configuration structure
struct EngineConfiguration {
    // Queue sizes
    size_t market_data_queue_size = 10000;  // Increased from default
    size_t order_queue_size = 5000;         // Increased from default
    
    // Batch processing
    size_t batch_size = 1000;               // Process data in batches
    
    // Thread priorities
    int strategy_thread_priority = 99;      // Real-time priority
    int execution_thread_priority = 99;     // Real-time priority
};

class Engine {
private:
    // Strategies
    std::vector<std::shared_ptr<winter::strategy::StrategyBase>> strategies_;
    
    // Portfolio
    Portfolio portfolio_;
    
    // Queues - with capacity as template parameter
    utils::LockFreeQueue<MarketData, 31000> market_data_queue_; // Increase from 10000 to 100000

    utils::LockFreeQueue<Order, 5000> order_queue_;
    
    // Threads
    std::thread strategy_thread_;
    std::thread execution_thread_;
    
    // Control flags
    std::atomic<bool> running_;
    
    // Callbacks
    std::function<void(const Order&)> order_callback_;
    
    // Configuration
    EngineConfiguration config_;
    
    // Thread functions
    void strategy_loop();
    void execution_loop();
    
public:
    Engine();
    ~Engine();
    
    // Configuration
    void configure(const EngineConfiguration& config);
    
    // Strategy management
    void add_strategy(std::shared_ptr<winter::strategy::StrategyBase> strategy);
    
    // Market data processing
    void process_market_data(const MarketData& data);
    void process_market_data_batch(const std::vector<MarketData>& batch);
    
    // Engine control
    void start(int strategy_core = -1, int execution_core = -1);
    void stop();
    
    // Portfolio access
    Portfolio& portfolio() { return portfolio_; }
    const Portfolio& portfolio() const { return portfolio_; }
    
    // Callbacks
    void set_order_callback(std::function<void(const Order&)> callback);
};

} // namespace winter::core
