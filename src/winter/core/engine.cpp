#include <winter/core/engine.hpp>
#include <winter/utils/logger.hpp>
#include <winter/utils/core_affinity.hpp>
#include <chrono>
#include <thread>
#include <algorithm>

namespace winter::core {

Engine::Engine() : running_(false) {}

Engine::~Engine() {
    stop();
}

void Engine::add_strategy(strategy::StrategyPtr strategy) {
    strategies_.push_back(strategy);
    utils::Logger::info() << "Added strategy: " << strategy->name() << utils::Logger::endl;
}

void Engine::process_market_data(const MarketData& data) {
    // Allocate from memory pool
    MarketData* data_ptr = market_data_pool_.allocate();
    *data_ptr = data;
    
    // Push to queue for processing
    if (!market_data_queue_.push(data_ptr)) {
        // Queue is full, deallocate and log error
        market_data_pool_.deallocate(data_ptr);
        utils::Logger::error() << "Market data queue full, dropping data for " 
                              << data.symbol << utils::Logger::endl;
    }
}

void Engine::start(int strategy_core_id, int execution_core_id) {
    if (running_) {
        utils::Logger::warn() << "Engine already running" << utils::Logger::endl;
        return;
    }
    
    running_ = true;
    
    // Initialize all strategies
    for (auto& strategy : strategies_) {
        strategy->initialize();
    }
    
    // Start strategy thread - fixed to use standard thread creation
    if (strategy_core_id >= 0) {
        strategy_thread_ = std::make_unique<std::thread>([this, strategy_core_id]() {
            // Pin the thread after creation
            utils::CoreAffinity::pin_thread_to_core(
                pthread_self(), strategy_core_id);
            this->strategy_loop();
        });
    } else {
        strategy_thread_ = std::make_unique<std::thread>(
            [this]() { this->strategy_loop(); }
        );
    }
    
    // Start execution thread - fixed to use standard thread creation
    if (execution_core_id >= 0) {
        execution_thread_ = std::make_unique<std::thread>([this, execution_core_id]() {
            // Pin the thread after creation
            utils::CoreAffinity::pin_thread_to_core(
                pthread_self(), execution_core_id);
            this->execution_loop();
        });
    } else {
        execution_thread_ = std::make_unique<std::thread>(
            [this]() { this->execution_loop(); }
        );
    }
    
    utils::Logger::info() << "Engine started" << utils::Logger::endl;
}

void Engine::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    if (strategy_thread_ && strategy_thread_->joinable()) {
        strategy_thread_->join();
        strategy_thread_.reset();
    }
    
    if (execution_thread_ && execution_thread_->joinable()) {
        execution_thread_->join();
        execution_thread_.reset();
    }
    
    // Shutdown all strategies
    for (auto& strategy : strategies_) {
        strategy->shutdown();
    }
    
    utils::Logger::info() << "Engine stopped" << utils::Logger::endl;
}

void Engine::strategy_loop() {
    utils::Logger::info() << "Strategy thread started" << utils::Logger::endl;
    
    while (running_) {
        // Process market data from queue
        auto data_opt = market_data_queue_.pop();
        if (data_opt) {
            MarketData* data_ptr = *data_opt;  // Fixed: properly extract from optional
            
            // Process through all strategies
            for (auto& strategy : strategies_) {
                if (strategy->is_enabled()) {
                    // Get signals from strategy
                    auto signals = strategy->process_tick(*data_ptr);  // Now data_ptr is properly dereferenced
                    
                    // Process signals
                    for (const auto& signal : signals) {
                        // Convert signal to order
                        Order order;
                        order.symbol = signal.symbol;
                        order.price = signal.price;
                        
                        // Determine order side and quantity based on signal
                        if (signal.type == SignalType::BUY) {
                            order.side = OrderSide::BUY;
                            // Calculate quantity based on portfolio cash and risk limits
                            double max_position = portfolio_.cash() * 0.1; // 10% of cash per position
                            order.quantity = static_cast<int>(max_position / signal.price);
                        } 
                        else if (signal.type == SignalType::SELL) {
                            order.side = OrderSide::SELL;
                            // Sell existing position if any
                            order.quantity = portfolio_.get_position(signal.symbol);
                        }
                        else if (signal.type == SignalType::EXIT) {
                            // Exit position - determine side based on current position
                            int position = portfolio_.get_position(signal.symbol);
                            if (position > 0) {
                                order.side = OrderSide::SELL;
                                order.quantity = position;
                            } else if (position < 0) {
                                order.side = OrderSide::BUY;
                                order.quantity = -position; // Cover short position
                            } else {
                                // No position to exit
                                continue;
                            }
                        }
                        
                        // Skip if quantity is zero
                        if (order.quantity <= 0) {
                            continue;
                        }
                        
                        // Add order to execution queue
                        if (!order_queue_.push(order)) {
                            utils::Logger::error() << "Order queue full, dropping order for " 
                                                  << order.symbol << utils::Logger::endl;
                        }
                    }
                }
            }
            
            // Return data to pool
            market_data_pool_.deallocate(data_ptr);
        } else {
            // No data available, sleep briefly to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
    
    utils::Logger::info() << "Strategy thread stopped" << utils::Logger::endl;
}

void Engine::execution_loop() {
    utils::Logger::info() << "Execution thread started" << utils::Logger::endl;
    
    while (running_) {
        // Process orders from queue
        auto order_opt = order_queue_.pop();
        if (order_opt) {
            const Order& order = *order_opt;
            
            // Update portfolio
            if (order.side == OrderSide::BUY) {
                double cost = order.price * order.quantity;
                if (portfolio_.cash() >= cost) {
                    portfolio_.add_position(order.symbol, order.quantity, cost);
                    portfolio_.reduce_cash(cost);
                    
                    // Call order callback if registered
                    if (order_callback_) {
                        order_callback_(order);
                    }
                } else {
                    utils::Logger::warn() << "Insufficient cash for order: " 
                                         << order.symbol << utils::Logger::endl;
                }
            } else { // SELL
                int position = portfolio_.get_position(order.symbol);
                if (position >= order.quantity) {
                    double proceeds = order.price * order.quantity;
                    portfolio_.reduce_position(order.symbol, order.quantity);
                    portfolio_.add_cash(proceeds);
                    
                    // Call order callback if registered
                    if (order_callback_) {
                        order_callback_(order);
                    }
                } else {
                    utils::Logger::warn() << "Insufficient position for order: " 
                                         << order.symbol << utils::Logger::endl;
                }
            }
        } else {
            // No orders available, sleep briefly to reduce CPU usage
            std::this_thread::sleep_for(std::chrono::microseconds(1));
        }
    }
    
    utils::Logger::info() << "Execution thread stopped" << utils::Logger::endl;
}

} // namespace winter::core
