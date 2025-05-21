// applications/backtest_app/main.cpp
#include "winter/core/engine.hpp"
#include "winter/strategy/strategy_factory.hpp"
#include "winter/utils/config.hpp"
#include "winter/utils/logger.hpp"
#include "winter/backtest/backtest_engine.hpp"
#include <iostream>
#include <string>
#include <memory>

int main(int argc, char** argv) {
    try {
        // Load configuration
        auto config = winter::utils::Config::instance();
        if (!config->load_from_file("winter.conf")) {
            std::cerr << "Failed to load configuration file. Using defaults." << std::endl;
        }
        
        // Create backtest engine
        winter::backtest::BacktestEngine backtest;
        
        // Load data
        std::string data_file = config->get("data_file", "data.csv");
        if (!backtest.load_data(data_file)) {
            std::cerr << "Failed to load data from " << data_file << std::endl;
            return 1;
        }
        
        // Create strategy
        std::string strategy_type = config->get("strategy_type", "SimpleMAStrategy");
        auto strategy = winter::strategy::StrategyFactory::create_strategy(strategy_type);
        if (!strategy) {
            std::cerr << "Failed to create strategy of type " << strategy_type << std::endl;
            return 1;
        }
        
        // Configure strategy
        std::unordered_map<std::string, std::string> strategy_config;
        strategy_config["fast_period"] = config->get("fast_period", "10");
        strategy_config["slow_period"] = config->get("slow_period", "30");
        strategy->configure(strategy_config);
        
        // Add strategy to backtest
        backtest.add_strategy(strategy);
        
        // Run backtest
        std::cout << "Running backtest..." << std::endl;
        backtest.run();
        
        // Display results
        auto results = backtest.get_results();
        std::cout << "Backtest completed." << std::endl;
        std::cout << "Total trades: " << results.total_trades << std::endl;
        std::cout << "Win rate: " << (results.win_rate * 100.0) << "%" << std::endl;
        std::cout << "Profit factor: " << results.profit_factor << std::endl;
        std::cout << "Sharpe ratio: " << results.sharpe_ratio << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
