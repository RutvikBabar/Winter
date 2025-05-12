#include <gtest/gtest.h>
#include <winter/strategy/strategy_base.hpp>
#include <winter/strategy/strategy_registry.hpp>
#include <winter/core/market_data.hpp>
#include <winter/core/signal.hpp>

#include <vector>
#include <memory>
#include <string>

// Test strategy that generates signals based on price thresholds
class ThresholdStrategy : public winter::strategy::StrategyBase {
private:
    double buy_threshold_;
    double sell_threshold_;
    
public:
    ThresholdStrategy(const std::string& name, double buy_threshold, double sell_threshold)
        : StrategyBase(name), buy_threshold_(buy_threshold), sell_threshold_(sell_threshold) {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        if (data.price < buy_threshold_) {
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::BUY;
            signal.strength = 1.0 - (data.price / buy_threshold_);
            signal.price = data.price;
            signals.push_back(signal);
        }
        else if (data.price > sell_threshold_) {
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::SELL;
            signal.strength = (data.price / sell_threshold_) - 1.0;
            signal.price = data.price;
            signals.push_back(signal);
        }
        
        return signals;
    }
};

// Test fixture for Strategy tests
class StrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        winter::strategy::StrategyRegistry::clear();
    }
    
    void TearDown() override {
        winter::strategy::StrategyRegistry::clear();
    }
};

// Test strategy signal generation
TEST_F(StrategyTest, SignalGeneration) {
    auto strategy = std::make_shared<ThresholdStrategy>("TestStrategy", 100.0, 200.0);
    
    // Test buy signal
    winter::core::MarketData buy_data;
    buy_data.symbol = "AAPL";
    buy_data.price = 90.0;
    buy_data.volume = 1000;
    
    auto buy_signals = strategy->process_tick(buy_data);
    ASSERT_EQ(buy_signals.size(), 1);
    EXPECT_EQ(buy_signals[0].symbol, "AAPL");
    EXPECT_EQ(buy_signals[0].type, winter::core::SignalType::BUY);
    EXPECT_GT(buy_signals[0].strength, 0.0);
    EXPECT_EQ(buy_signals[0].price, 90.0);
    
    // Test sell signal
    winter::core::MarketData sell_data;
    sell_data.symbol = "AAPL";
    sell_data.price = 220.0;
    sell_data.volume = 1000;
    
    auto sell_signals = strategy->process_tick(sell_data);
    ASSERT_EQ(sell_signals.size(), 1);
    EXPECT_EQ(sell_signals[0].symbol, "AAPL");
    EXPECT_EQ(sell_signals[0].type, winter::core::SignalType::SELL);
    EXPECT_GT(sell_signals[0].strength, 0.0);
    EXPECT_EQ(sell_signals[0].price, 220.0);
    
    // Test no signal
    winter::core::MarketData neutral_data;
    neutral_data.symbol = "AAPL";
    neutral_data.price = 150.0;
    neutral_data.volume = 1000;
    
    auto neutral_signals = strategy->process_tick(neutral_data);
    EXPECT_EQ(neutral_signals.size(), 0);
}

// Test strategy registry
TEST_F(StrategyTest, StrategyRegistry) {
    // Register strategies
    auto strategy1 = std::make_shared<ThresholdStrategy>("Strategy1", 100.0, 200.0);
    auto strategy2 = std::make_shared<ThresholdStrategy>("Strategy2", 150.0, 250.0);
    
    winter::strategy::StrategyRegistry::register_strategy(strategy1);
    winter::strategy::StrategyRegistry::register_strategy(strategy2);
    
    // Get all strategies
    auto strategies = winter::strategy::StrategyRegistry::get_all_strategies();
    EXPECT_EQ(strategies.size(), 2);
    
    // Get specific strategy
    auto retrieved_strategy = winter::strategy::StrategyRegistry::get_strategy("Strategy1");
    ASSERT_NE(retrieved_strategy, nullptr);
    EXPECT_EQ(retrieved_strategy->name(), "Strategy1");
    
    // Unregister strategy
    winter::strategy::StrategyRegistry::unregister_strategy("Strategy1");
    strategies = winter::strategy::StrategyRegistry::get_all_strategies();
    EXPECT_EQ(strategies.size(), 1);
    
    // Try to get unregistered strategy
    retrieved_strategy = winter::strategy::StrategyRegistry::get_strategy("Strategy1");
    EXPECT_EQ(retrieved_strategy, nullptr);
    
    // Clear registry
    winter::strategy::StrategyRegistry::clear();
    strategies = winter::strategy::StrategyRegistry::get_all_strategies();
    EXPECT_EQ(strategies.size(), 0);
}

// Test helper template for creating and registering strategies
TEST_F(StrategyTest, CreateAndRegister) {
    auto strategy = winter::strategy::StrategyRegistry::create_and_register<ThresholdStrategy>(
        "TestStrategy", 100.0, 200.0
    );
    
    ASSERT_NE(strategy, nullptr);
    EXPECT_EQ(strategy->name(), "TestStrategy");
    
    auto strategies = winter::strategy::StrategyRegistry::get_all_strategies();
    EXPECT_EQ(strategies.size(), 1);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
