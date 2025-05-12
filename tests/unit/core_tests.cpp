#include <gtest/gtest.h>
#include <winter/core/engine.hpp>
#include <winter/core/market_data.hpp>
#include <winter/core/signal.hpp>
#include <winter/core/order.hpp>
#include <winter/core/portfolio.hpp>
#include <winter/strategy/strategy_base.hpp>

#include <vector>
#include <memory>
#include <string>

// Test strategy that always generates a buy signal
class TestBuyStrategy : public winter::strategy::StrategyBase {
public:
    TestBuyStrategy() : StrategyBase("TestBuyStrategy") {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        winter::core::Signal signal;
        signal.symbol = data.symbol;
        signal.type = winter::core::SignalType::BUY;
        signal.strength = 1.0;
        signal.price = data.price;
        
        signals.push_back(signal);
        return signals;
    }
};

// Test strategy that always generates a sell signal
class TestSellStrategy : public winter::strategy::StrategyBase {
public:
    TestSellStrategy() : StrategyBase("TestSellStrategy") {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        winter::core::Signal signal;
        signal.symbol = data.symbol;
        signal.type = winter::core::SignalType::SELL;
        signal.strength = 1.0;
        signal.price = data.price;
        
        signals.push_back(signal);
        return signals;
    }
};

// Test fixture for Engine tests
class EngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        engine = std::make_unique<winter::core::Engine>();
    }
    
    std::unique_ptr<winter::core::Engine> engine;
};

// Test adding strategies to the engine
TEST_F(EngineTest, AddStrategy) {
    auto strategy1 = std::make_shared<TestBuyStrategy>();
    auto strategy2 = std::make_shared<TestSellStrategy>();
    
    engine->add_strategy(strategy1);
    engine->add_strategy(strategy2);
    
    // No assertion needed - if it doesn't crash, it works
}

// Test market data processing
TEST_F(EngineTest, ProcessMarketData) {
    auto strategy = std::make_shared<TestBuyStrategy>();
    engine->add_strategy(strategy);
    
    winter::core::MarketData data;
    data.symbol = "AAPL";
    data.price = 150.0;
    data.volume = 1000;
    
    // Set initial portfolio cash
    engine->portfolio().set_cash(10000.0);
    
    // Start the engine
    engine->start();
    
    // Process market data
    engine->process_market_data(data);
    
    // Give some time for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Stop the engine
    engine->stop();
    
    // Check if a position was created
    EXPECT_GT(engine->portfolio().get_position("AAPL"), 0);
}

// Test portfolio functionality
TEST(PortfolioTest, BasicOperations) {
    winter::core::Portfolio portfolio;
    
    // Test initial state
    EXPECT_EQ(portfolio.cash(), 0.0);
    EXPECT_EQ(portfolio.get_position("AAPL"), 0);
    
    // Test setting cash
    portfolio.set_cash(10000.0);
    EXPECT_EQ(portfolio.cash(), 10000.0);
    
    // Test adding position
    portfolio.add_position("AAPL", 10, 1500.0);
    EXPECT_EQ(portfolio.get_position("AAPL"), 10);
    EXPECT_EQ(portfolio.get_position_cost("AAPL"), 1500.0);
    
    // Test reducing cash
    portfolio.reduce_cash(1500.0);
    EXPECT_EQ(portfolio.cash(), 8500.0);
    
    // Test reducing position
    portfolio.reduce_position("AAPL", 5);
    EXPECT_EQ(portfolio.get_position("AAPL"), 5);
    
    // Test adding cash
    portfolio.add_cash(750.0);
    EXPECT_EQ(portfolio.cash(), 9250.0);
    
    // Test total value
    EXPECT_EQ(portfolio.total_value(), 9250.0 + portfolio.get_position_cost("AAPL"));
}

// Test signal creation
TEST(SignalTest, Creation) {
    winter::core::Signal signal("AAPL", winter::core::SignalType::BUY, 0.8, 150.0);
    
    EXPECT_EQ(signal.symbol, "AAPL");
    EXPECT_EQ(signal.type, winter::core::SignalType::BUY);
    EXPECT_EQ(signal.strength, 0.8);
    EXPECT_EQ(signal.price, 150.0);
}

// Test order creation
TEST(OrderTest, Creation) {
    winter::core::Order order("AAPL", winter::core::OrderSide::BUY, 10, 150.0);
    
    EXPECT_EQ(order.symbol, "AAPL");
    EXPECT_EQ(order.side, winter::core::OrderSide::BUY);
    EXPECT_EQ(order.quantity, 10);
    EXPECT_EQ(order.price, 150.0);
    EXPECT_EQ(order.total_value(), 1500.0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
