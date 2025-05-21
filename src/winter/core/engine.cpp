#include <winter/core/engine.hpp>
#include <winter/utils/logger.hpp>
#include <algorithm>
#include <execution>
#include <windows.h>


namespace winter::core {

Engine::Engine() 
    : running_(false) {
    // Initialize with default configuration
    EngineConfiguration default_config;
    config_ = default_config;
}

Engine::~Engine() {
    stop();
}

void Engine::configure(const EngineConfiguration& config) {
    config_ = config;
    
    // Note: We can't resize the queues since they're fixed-size template parameters
    // If you need different sizes, you'd need to create new queues
}

void Engine::add_strategy(std::shared_ptr<winter::strategy::StrategyBase> strategy) {
    strategies_.push_back(strategy);
}

void Engine::process_market_data(const MarketData& data) {
    if (!market_data_queue_.push(data)) {
        utils::Logger::error() << "Market data queue full, dropping data for " << data.symbol << utils::Logger::endl;
    }
}

void Engine::process_market_data_batch(const std::vector<MarketData>& batch) {
    // Process batch in parallel
    std::for_each(std::execution::par_unseq, 
                 batch.begin(), batch.end(),
                 [this](const MarketData& data) {
                     this->process_market_data(data);
                 });
}

void Engine::start(int strategy_core, int execution_core) {
    if (running_) {
        utils::Logger::warn() << "Engine already running" << utils::Logger::endl;
        return;
    }
    
    running_ = true;
    
    // Start threads
    auto strategy_func = [this]() { strategy_loop(); };
    auto execution_func = [this]() { execution_loop(); };
    
    if (strategy_core >= 0) {
        // Pin thread to specific core
        strategy_thread_ = std::thread([strategy_core, strategy_func]() {
            // Pin thread to core
            #ifdef _WIN32
            SetThreadAffinityMask(GetCurrentThread(), (1ULL << strategy_core));
            #else
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(strategy_core, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            #endif
            
            // Run the function
            strategy_func();
        });
    } else {
        strategy_thread_ = std::thread(strategy_func);
    }
    
    if (execution_core >= 0) {
        // Pin thread to specific core
        execution_thread_ = std::thread([execution_core, execution_func]() {
            // Pin thread to core
            #ifdef _WIN32
            SetThreadAffinityMask(GetCurrentThread(), (1ULL << execution_core));
            #else
            cpu_set_t cpuset;
            CPU_ZERO(&cpuset);
            CPU_SET(execution_core, &cpuset);
            pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
            #endif
            
            // Run the function
            execution_func();
        });
    } else {
        execution_thread_ = std::thread(execution_func);
    }
    
    utils::Logger::info() << "Engine started" << utils::Logger::endl;
}

void Engine::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Wait for threads to finish
    if (strategy_thread_.joinable()) {
        strategy_thread_.join();
    }
    
    if (execution_thread_.joinable()) {
        execution_thread_.join();
    }
    
    utils::Logger::info() << "Engine stopped" << utils::Logger::endl;
}

void Engine::set_order_callback(std::function<void(const Order&)> callback) {
    order_callback_ = callback;
}

void Engine::strategy_loop() {
    utils::Logger::info() << "Strategy thread started" << utils::Logger::endl;
    
    // Batch processing variables
    std::vector<MarketData> data_batch;
    data_batch.reserve(config_.batch_size);
    
    while (running_) {
        // Process market data in batches
        MarketData data;
        while (market_data_queue_.pop(data) && data_batch.size() < config_.batch_size) {
            data_batch.push_back(data);
        }
        
        if (!data_batch.empty()) {
            // Process the batch with each strategy
            for (auto& strategy : strategies_) {
                if (strategy->is_enabled()) {
                    // Process each data point with the strategy
                    for (const auto& d : data_batch) {
                        // Get signals from process_tick
                        std::vector<winter::core::Signal> signals = strategy->process_tick(d);
                        
                        // Process each signal
                        for (const auto& signal : signals) {
                            // Handle the signal
                            if (signal.type != SignalType::NEUTRAL) {
                                Order order;
                                order.symbol = signal.symbol;
                                
                                if (signal.type == SignalType::BUY) {
                                    order.side = OrderSide::BUY;
                                    
                                    // Calculate position size based on signal strength and available cash
                                    double max_position = portfolio_.cash() * 0.1; // Max 10% of portfolio per position
                                    int quantity = static_cast<int>(max_position / signal.price);
                                    
                                    if (quantity > 0) {
                                        order.quantity = quantity;
                                        order.price = signal.price;
                                        
                                        if (!order_queue_.push(order)) {
                                            utils::Logger::error() << "Order queue full, dropping order for " << order.symbol << utils::Logger::endl;
                                        }
                                    }
                                }
                                else if (signal.type == SignalType::SELL) {
                                    order.side = OrderSide::SELL;
                                    
                                    // Get current position
                                    int position = portfolio_.get_position(signal.symbol);
                                    
                                    if (position > 0) {
                                        order.quantity = position; // Sell entire position
                                        order.price = signal.price;
                                        
                                        if (!order_queue_.push(order)) {
                                            utils::Logger::error() << "Order queue full, dropping order for " << order.symbol << utils::Logger::endl;
                                        }
                                    }
                                    // Don't log warnings here - we'll handle that in the execution loop
                                }
                            }
                        }
                    }
                }
            }
            
            // Clear the batch
            data_batch.clear();
        }
        
        // Yield to other threads if no data
        if (data_batch.empty()) {
            std::this_thread::yield();
        }
    }
}

void Engine::execution_loop() {
    utils::Logger::info() << "Execution thread started" << utils::Logger::endl;
    
    // Batch processing for orders
    std::vector<Order> order_batch;
    order_batch.reserve(config_.batch_size);
    
    // Keep track of positions for backtesting
    std::unordered_map<std::string, int> positions;
    
    while (running_) {
        // Collect orders in a batch
        Order order;
        while (order_queue_.pop(order) && order_batch.size() < config_.batch_size) {
            order_batch.push_back(order);
        }
        
        if (!order_batch.empty()) {
            // Process orders
            for (const auto& o : order_batch) {
                if (o.side == OrderSide::BUY) {
                    double cost = o.price * o.quantity;
                    
                    if (portfolio_.cash() >= cost) {
                        // Execute buy order
                        portfolio_.reduce_cash(cost);
                        portfolio_.add_position(o.symbol, o.quantity, cost);
                        
                        // Update position tracking
                        positions[o.symbol] += o.quantity;
                        
                        // Call order callback
                        if (order_callback_) {
                            order_callback_(o);
                        }
                    }
                    else {
                        utils::Logger::warn() << "Insufficient cash for order: " << o.symbol << utils::Logger::endl;
                    }
                }
                else if (o.side == OrderSide::SELL) {
                    int position = portfolio_.get_position(o.symbol);
                    
                    // Fix: Check if we have enough position to sell
                    if (position >= o.quantity) {
                        // Execute sell order
                        double proceeds = o.price * o.quantity;
                        portfolio_.add_cash(proceeds);
                        portfolio_.reduce_position(o.symbol, o.quantity);
                        
                        // Update position tracking
                        positions[o.symbol] -= o.quantity;
                        
                        // Call order callback
                        if (order_callback_) {
                            order_callback_(o);
                        }
                    }
                    else if (position > 0) {
                        // We have some position but not enough - sell what we have
                        utils::Logger::info() << "Partial position for " << o.symbol 
                                            << ": requested " << o.quantity 
                                            << ", available " << position 
                                            << ". Selling available position." << utils::Logger::endl;
                        
                        // Create a modified order with the available quantity
                        Order modified_order = o;
                        modified_order.quantity = position;
                        
                        // Execute the modified sell order
                        double proceeds = modified_order.price * modified_order.quantity;
                        portfolio_.add_cash(proceeds);
                        portfolio_.reduce_position(modified_order.symbol, modified_order.quantity);
                        
                        // Update position tracking
                        positions[o.symbol] = 0;
                        
                        // Call order callback with modified order
                        if (order_callback_) {
                            order_callback_(modified_order);
                        }
                    }
                    else {
                        // We have no position at all - ignore the order
                        utils::Logger::debug() << "Ignored sell order for " << o.symbol << " - no position" << utils::Logger::endl;
                    }
                }
            }
            
            // Clear the batch
            order_batch.clear();
        }
        
        // Yield to other threads if no orders
        if (order_batch.empty()) {
            std::this_thread::yield();
        }
    }
}

} // namespace winter::core
