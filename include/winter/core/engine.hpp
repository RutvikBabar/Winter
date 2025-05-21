// include/winter/core/engine.hpp
#pragma once
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "winter/core/market_data.hpp"
#include "winter/core/order.hpp"
#include "winter/core/portfolio.hpp"
#include "winter/strategy/strategy_base.hpp"
#include "winter/utils/lock_free_queue.hpp"
#include <functional>

namespace winter {
namespace core {

// Engine configuration structure
struct EngineConfiguration {
    // Queue sizes
    size_t market_data_queue_size = 1000000;
    size_t order_queue_size = 500000;
    
    // Processing
    size_t batch_size = 10000;
    
    // Logging
    bool enable_logging = true;
    std::string log_level = "info";
    
    // Execution mode
    enum class ExecutionMode { BACKTEST, PAPER_TRADING, LIVE_TRADING };
    ExecutionMode execution_mode = ExecutionMode::BACKTEST;
};

class Engine {
private:
    // Strategies
    std::vector<strategy::StrategyPtr> strategies_;
    std::mutex strategies_mutex_;
    
    // Portfolio
    Portfolio portfolio_;
    
    // Queues
    utils::LockFreeQueue<MarketData> market_data_queue_; 
    utils::LockFreeQueue<Order> order_queue_;
    
    // Threads
    std::thread strategy_thread_;
    std::thread execution_thread_;
    
    // Control flags
    std::atomic<bool> running_{false};
    std::condition_variable cv_;
    std::mutex cv_mutex_;
    // Callback for order processing
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
    void add_strategy(strategy::StrategyPtr strategy);
    void remove_strategy(const std::string& name);
    strategy::StrategyPtr get_strategy(const std::string& name);
    // Add this to process market data in batches
    void process_market_data_batch(const std::vector<MarketData>& batch);

    // Update the start method to accept core affinity parameters
    void start(int strategy_core = -1, int execution_core = -1);

    // Add callback functionality
    void set_order_callback(std::function<void(const Order&)> callback);

    // Market data processing
    void process_market_data(const MarketData& data);
    
    // Engine control
    void stop();
    bool is_running() const { return running_; }
    
    // Portfolio access
    Portfolio& portfolio() { return portfolio_; }
};

} // namespace core
} // namespace winter
