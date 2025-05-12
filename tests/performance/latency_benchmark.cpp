#include <winter/core/engine.hpp>
#include <winter/strategy/strategy_registry.hpp>
#include <winter/utils/logger.hpp>
#include <winter/utils/flamegraph.hpp>

#include <chrono>
#include <iostream>
#include <vector>
#include <string>
#include <random>

// Simple strategy for benchmarking
class BenchmarkStrategy : public winter::strategy::StrategyBase {
private:
    std::mt19937 rng_;
    std::uniform_real_distribution<double> dist_;
    
public:
    BenchmarkStrategy() 
        : StrategyBase("BenchmarkStrategy"), 
          rng_(std::random_device{}()),
          dist_(-1.0, 1.0) {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        // Generate random signal with 10% probability
        if (dist_(rng_) > 0.8) {
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.price = data.price;
            signal.strength = std::abs(dist_(rng_));
            
            if (dist_(rng_) > 0.0) {
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

int main(int argc, char* argv[]) {
    // Set log level to reduce output
    winter::utils::Logger::set_level(winter::utils::LogLevel::WARN);
    
    // Parse command line arguments
    int num_strategies = 1;
    int num_ticks = 100000;
    
    if (argc > 1) {
        num_strategies = std::stoi(argv[1]);
    }
    if (argc > 2) {
        num_ticks = std::stoi(argv[2]);
    }
    
    std::cout << "Running latency benchmark with " << num_strategies 
              << " strategies and " << num_ticks << " ticks" << std::endl;
    
    // Create engine
    winter::core::Engine engine;
    
    // Register strategies
    for (int i = 0; i < num_strategies; ++i) {
        auto strategy = std::make_shared<BenchmarkStrategy>();
        engine.add_strategy(strategy);
    }
    
    // Start flamegraph profiling
    winter::utils::Flamegraph flamegraph("latency_benchmark");
    flamegraph.start();
    
    // Start engine
    engine.start();
    
    // Generate and process market data
    std::mt19937 rng(std::random_device{}());
    
    auto start_time = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < num_ticks; ++i) {
        auto data = generate_market_data(rng);
        engine.process_market_data(data);
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
    double avg_latency = static_cast<double>(duration) / num_ticks;
    double ticks_per_second = static_cast<double>(num_ticks) * 1000000.0 / duration;
    
    std::cout << "Benchmark results:" << std::endl;
    std::cout << "Total time: " << duration << " us" << std::endl;
    std::cout << "Average latency per tick: " << avg_latency << " us" << std::endl;
    std::cout << "Ticks processed per second: " << ticks_per_second << std::endl;
    
    return 0;
}
