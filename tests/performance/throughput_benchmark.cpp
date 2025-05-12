#include <winter/core/engine.hpp>
#include <winter/strategy/strategy_registry.hpp>
#include <winter/utils/logger.hpp>
#include <winter/utils/flamegraph.hpp>

#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <random>
#include <thread>
#include <atomic>

// Simple strategy for benchmarking
class ThroughputStrategy : public winter::strategy::StrategyBase {
private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    
public:
    ThroughputStrategy() 
        : StrategyBase("ThroughputStrategy"), 
          rng_(std::random_device{}()),
          dist_(0.0, 1.0) {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        // Generate random signal with 5% probability
        if (dist_(rng_) > 0.95) {
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.price = data.price;
            signal.strength = dist_(rng_);
            
            if (dist_(rng_) > 0.5) {
                signal.type = winter::core::SignalType::BUY;
            } else {
                signal.type = winter::core::SignalType::SELL;
            }
            
            signals.push_back(signal);
        }
        
        return signals;
    }
};

// Function to generate random market data
winter::core::MarketData generate_market_data(std::mt19937& rng) {
    static const std::vector<std::string> symbols = {
        "AAPL", "MSFT", "GOOGL", "AMZN", "META", "TSLA", "NVDA", "JPM"
    };
    
    std::uniform_int_distribution<size_t> symbol_dist(0, symbols.size() - 1);
    std::uniform_real_distribution<double> price_dist(100.0, 1000.0);
    std::uniform_int_distribution<int> volume_dist(100, 10000);
    
    winter::core::MarketData data;
    data.symbol = symbols[symbol_dist(rng)];
    data.price = price_dist(rng);
    data.volume = volume_dist(rng);
    data.timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()
    ).count();
    
    return data;
}

// Producer thread function
void producer_thread(winter::core::Engine& engine, int num_ticks, std::atomic<int>& ticks_processed) {
    std::mt19937 rng(std::random_device{}());
    
    for (int i = 0; i < num_ticks; ++i) {
        auto data = generate_market_data(rng);
        engine.process_market_data(data);
        ticks_processed.fetch_add(1, std::memory_order_relaxed);
    }
}

int main(int argc, char* argv[]) {
    // Set log level to reduce output
    winter::utils::Logger::set_level(winter::utils::LogLevel::WARN);
    
    // Parse command line arguments
    int num_strategies = 1;
    int num_producers = 1;
    int num_ticks_per_producer = 100000;
    int duration_seconds = 10;
    
    if (argc > 1) {
        num_strategies = std::stoi(argv[1]);
    }
    if (argc > 2) {
        num_producers = std::stoi(argv[2]);
    }
    if (argc > 3) {
        num_ticks_per_producer = std::stoi(argv[3]);
    }
    if (argc > 4) {
        duration_seconds = std::stoi(argv[4]);
    }
    
    std::cout << "Running throughput benchmark with " << num_strategies 
              << " strategies and " << num_producers << " producers" << std::endl;
    
    // Create engine
    winter::core::Engine engine;
    
    // Register strategies
    for (int i = 0; i < num_strategies; ++i) {
        auto strategy = std::make_shared<ThroughputStrategy>();
        engine.add_strategy(strategy);
    }
    
    // Start flamegraph profiling
    winter::utils::Flamegraph flamegraph("throughput_benchmark");
    flamegraph.start();
    
    // Start engine
    engine.start();
    
    // Start producer threads
    std::vector<std::thread> producer_threads;
    std::atomic<int> ticks_processed(0);
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_producers; ++i) {
        producer_threads.emplace_back(
            producer_thread, std::ref(engine), num_ticks_per_producer, std::ref(ticks_processed)
        );
    }
    
    // Monitor progress
    int last_ticks = 0;
    for (int i = 0; i < duration_seconds; ++i) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        int current_ticks = ticks_processed.load(std::memory_order_relaxed);
        int ticks_this_second = current_ticks - last_ticks;
        last_ticks = current_ticks;
        
        std::cout << "Second " << (i+1) << ": " << ticks_this_second << " ticks/s" << std::endl;
    }
    
    // Wait for producer threads to finish
    for (auto& thread : producer_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        end_time - start_time
    ).count();
    
    // Stop engine
    engine.stop();
    
    // Stop flamegraph profiling
    flamegraph.stop();
    flamegraph.generate_report();
    
    // Calculate and print results
    int total_ticks = ticks_processed.load(std::memory_order_relaxed);
    double ticks_per_second = static_cast<double>(total_ticks) * 1000000.0 / duration;
    
    std::cout << "Benchmark results:" << std::endl;
    std::cout << "Total ticks processed: " << total_ticks << std::endl;
    std::cout << "Total time: " << duration / 1000000.0 << " s" << std::endl;
    std::cout << "Average throughput: " << ticks_per_second << " ticks/s" << std::endl;
    
    return 0;
}
