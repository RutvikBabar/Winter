// tests/integration/framework_integration_test.cpp
#include <gtest/gtest.h>
#include "winter/core/engine.hpp"
#include "winter/strategy/strategy_base.hpp"
#include "winter/core/market_data.hpp"
#include "winter/core/signal.hpp"
#include <memory>
#include <vector>

// Mock strategy for testing
class MockStrategy : public winter::strategy::StrategyBase {
public:
    MockStrategy() : StrategyBase("MockStrategy") {}
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        processed_ticks_++;
        
        if (should_generate_signal_) {
            winter::core::Signal signal;
            signal.symbol = data.symbol;
            signal.type = winter::core::SignalType::BUY;
            signal.price = data.price;
            signal.strength = 1.0;
            return {signal};
        }
        
        return {};
    }
    
    void set_generate_signal(bool generate) {
        should_generate_signal_ = generate;
    }
    
    int get_processed_ticks() const {
        return processed_ticks_;
    }
    
private:
    bool should_generate_signal_ = false;
    int processed_ticks_ = 0;
};

// Test fixture
class FrameworkIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Configure engine
        winter::core::EngineConfiguration config;
        engine_.configure(config);
        
        // Create and register mock strategy
        strategy_ = std::make_shared<MockStrategy>();
        engine_.add_strategy(strategy_);
    }
    
    winter::core::Engine engine_;
    std::shared_ptr<MockStrategy> strategy_;
};

// Test that market data is properly delivered to strategies
TEST_F(FrameworkIntegrationTest, MarketDataDelivery) {
    // Start the engine
    engine_.start();
    
    // Create sample market data
    winter::core::MarketData data;
    data.symbol = "AAPL";
    data.price = 150.0;
    data.timestamp = 1621500000000000; // Some timestamp
    
    // Process the data
    engine_.process_market_data(data);
    
    // Wait a bit for processing
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Verify the strategy received the tick
    EXPECT_EQ(strategy_->get_processed_ticks(), 1);
    
    // Stop the engine
    engine_.stop();
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
