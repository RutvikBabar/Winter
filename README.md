# Winter Trading Framework

A high-performance C++ backtesting and trading framework built for statistical arbitrage and pairs trading. Designed for speed, scalability, and real-time market data processing.

---

## Features

- **High Performance**: Process over 1M+ market data points with a multi-threaded architecture.
- **Statistical Arbitrage**: Built-in support for 30+ cointegrated pairs across sectors.
- **Advanced Risk Management**: Stop-loss, trailing stops, sector allocation limits.
- **Real-time Analytics**: Sharpe ratio, drawdown analysis, performance monitoring.
- **Configurable Strategies**: Strategy selection through lightweight configuration files.
- **Lock-free Processing**: Optimized for modern multi-core systems with minimal contention.
- **Comprehensive Reporting**: HTML reports and CSV exports supported.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Installation](#installation)
- [Usage](#usage)
- [Strategies](#strategies)
- [Performance](#performance)
- [Configuration](#configuration)
- [Contributing](#contributing)
- [Strategies](#Strategies)

---

## Quick Start

```bash
# Clone the repository
git clone https://github.com/RutvikBabar/Winter.git
cd Winter

# Build the framework
make

# Run a backtest
./build/simulate --backtest 1 your_market_data.csv
```

---

## Installation

### Prerequisites

- C++20-compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.16+ or Make
- ZeroMQ (for real-time market data feeds)

### Build Instructions

```bash
git clone https://github.com/RutvikBabar/Winter.git
cd Winter/Winter2.0
make clean && make

# Verify installation
./build/simulate --help
```

---

## Usage

### Strategy Configuration

Create `winter_strategies.conf`:

```conf
# Strategy ID mapping
1=StatArbitrage
2=MeanReversion
3=momentum_strategy
4=pairs_trading
```

### Running Backtests

```bash
# Run statistical arbitrage
./build/simulate --backtest 1 market_data.csv

# Run mean reversion
./build/simulate --backtest 2 market_data.csv

# Custom initial balance
./build/simulate --backtest 1 data.csv --initial-balance 10000000
```

### Market Data Format

```csv
timestamp,symbol,price,volume
1609459200000000,AAPL,132.69,100000
1609459200000000,MSFT,222.42,75000
```

### Sample Output

```
=== Backtest Results ===
Initial Capital: $5,000,000.00
Final Capital:   $5,005,608.99
Total Return:    $5,608.99 (0.11%)
Sharpe Ratio:      1.24
Max Drawdown:      8.67%
Total Trades:      8747
Win Rate:          65.50%
Profit Factor:     1.86
```

---

## Strategies

### Statistical Arbitrage

- 30 cointegrated pairs across sectors (Technology, Financials, Energy, ETFs)
- Multi-timeframe analysis (short, medium, long)
- Real-time beta calculation and dynamic hedge ratios
- Z-score based entries with confirmation logic

**Example Pairs**:

- Technology: `AAPL-MSFT`, `GOOGL-FB`, `AMD-NVDA`
- Financial: `JPM-BAC`, `C-WFC`, `GS-MS`
- Energy: `XOM-CVX`, `BP-SHEL`, `COP-MRO`
- ETFs: `SPY-IVV`, `QQQ-XLK`, `XLE-VDE`

### Mean Reversion

- RSI, Bollinger Bands, Volume Oscillator
- 200-period EMA for trend filtering
- ATR-based position sizing
- Multi-condition signal confirmation

---

## Performance

### Benchmarks

- Throughput: 15,000+ messages/second
- Latency: Sub-millisecond signal generation
- Queue capacity: 25M+ events
- Scalability: Up to 16 worker threads

### Optimization Features

- Lock-free ring buffers
- Adaptive batching
- Memory pools for fast allocation
- SIMD-optimized math routines
- Cache-friendly data layouts

---

## Configuration

### Strategy Parameters

```cpp
// Entry/Exit Thresholds
double ENTRY_THRESHOLD = 2.2;
double EXIT_THRESHOLD = 0.15;
double PROFIT_TARGET_MULT = 0.15;

// Risk Management
double STOP_LOSS_PCT = 0.008;
double MAX_POSITION_PCT = 0.002;
double MIN_CASH_RESERVE_PCT = 0.25;
```

### Performance Tuning

```cpp
// Threading Configuration
const int MAX_THREADS = 16;
const size_t MAX_QUEUE_SIZE = 25000000;
const size_t BATCH_SIZE = 100;
```

---

## Technical Architecture

### Core Pipeline

Market Data → Strategy Engine → Risk Management  
↓              ↓                ↓  
Parallel        Signal         Position  
Queues          Logic           Sizing

### Technologies

- C++20 (concepts, coroutines, ranges, `constexpr`)
- Multi-threaded lock-free architecture
- Template metaprogramming for zero-cost abstractions
- RAII-based memory safety

---

## Advanced Features

### Real-time Monitoring

```
Performance: 15420 msgs/sec
Drop Rate: 2.3%
Workers: 12/16
Cash Reserve: 78.5%
Active Positions: 15
Sharpe: 1.24
```

### Adaptive Systems

- Queue pressure-based throttling
- Emergency liquidation triggers
- Sector allocation rebalancing
- Performance-based scaling

---

## Analytics & Reporting

- HTML reports with interactive visualizations
- CSV exports for offline analysis
- Real-time metrics dashboard
- Full trade logs with timestamps and PnL

---

## Contributing

We welcome pull requests. Please follow the contribution process:

### Development Setup

```bash
# Fork and clone
git clone https://github.com/yourusername/Winter.git
cd Winter/Winter2.0

# Create a new feature branch
git checkout -b feature/your-feature

# Build and test
make clean && make
./build/simulate --backtest 1 test_data.csv
```

### Code Standards

- Follow C++20 best practices
- Use the Google C++ Style Guide
- Include unit tests and performance benchmarks for new code

---


## Strategies
Writing and Registering Custom strategies
Follow these step-by-step instructions to create and register your own trading strategy in the Winter framework.

## Step 1: Create Strategy Header File

Create a new header file in the `strategies/` directory (e.g., `strategies/my_custom_strategy.hpp`):

#pragma once
#include <winter/strategy/strategy_base.hpp>
#include <winter/strategy/strategy_factory.hpp>
#include <winter/core/signal.hpp>
#include <winter/core/market_data.hpp>
#include <deque>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <numeric>

class MyCustomStrategy : public winter::strategy::StrategyBase {
private:
    // Your strategy parameters
    double entry_threshold = 2.0;
    double exit_threshold = 0.5;
    
    // Data storage for each symbol
    std::unordered_map<std::string, std::deque<double>> price_history;
    std::unordered_map<std::string, int> positions;
    
    const int LOOKBACK_PERIOD = 20;
    const double MAX_POSITION_PCT = 0.01; // 1% of capital per position

public:
    // Constructor must accept string parameter for factory compatibility
    MyCustomStrategy(const std::string& name = "MyCustomStrategy") : StrategyBase(name) {}
    
    // Main strategy logic - override this method
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        std::vector<winter::core::Signal> signals;
        
        // Update price history
        price_history[data.symbol].push_back(data.price);
        if (price_history[data.symbol].size() > LOOKBACK_PERIOD) {
            price_history[data.symbol].pop_front();
        }
        
        // Only trade if we have enough history
        if (price_history[data.symbol].size() < LOOKBACK_PERIOD) {
            return signals;
        }
        
        // Calculate moving average
        double sum = std::accumulate(price_history[data.symbol].begin(), 
                                   price_history[data.symbol].end(), 0.0);
        double moving_average = sum / price_history[data.symbol].size();
        
        // Calculate standard deviation
        double variance = 0.0;
        for (double price : price_history[data.symbol]) {
            variance += (price - moving_average) * (price - moving_average);
        }
        double std_dev = std::sqrt(variance / price_history[data.symbol].size());
        
        // Calculate z-score
        double z_score = (data.price - moving_average) / std_dev;
        
        // Get current position
        int current_position = positions[data.symbol];
        
        // Generate signals based on z-score
        if (current_position == 0) {
            // Entry logic
            if (z_score > entry_threshold) {
                // Price is high, go short
                winter::core::Signal signal;
                signal.symbol = data.symbol;
                signal.type = winter::core::SignalType::SELL;
                signal.price = data.price;
                signal.strength = std::min(1.0, z_score / entry_threshold);
                signals.push_back(signal);
                
                positions[data.symbol] = -1; // Mark as short
            }
            else if (z_score < -entry_threshold) {
                // Price is low, go long
                winter::core::Signal signal;
                signal.symbol = data.symbol;
                signal.type = winter::core::SignalType::BUY;
                signal.price = data.price;
                signal.strength = std::min(1.0, -z_score / entry_threshold);
                signals.push_back(signal);
                
                positions[data.symbol] = 1; // Mark as long
            }
        }
        else {
            // Exit logic
            if (std::abs(z_score) < exit_threshold) {
                // Price has reverted, close position
                winter::core::Signal signal;
                signal.symbol = data.symbol;
                signal.type = current_position > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
                signal.price = data.price;
                signal.strength = 1.0;
                signals.push_back(signal);
                
                positions[data.symbol] = 0; // Close position
            }
        }
        
        return signals;
    }
};

// Register the strategy with the factory
namespace {
    bool my_custom_registered = []() {
        winter::strategy::StrategyFactory::register_type<MyCustomStrategy>("MyCustomStrategy");
        return true;
    }();
}

## Step 2: Include Strategy in Main File

Add your strategy header to `src/simulate/simulate.cpp`:

#include "strategies/stat_arbitrage.hpp"
#include "strategies/mean_reversion_strategy.hpp"
#include "strategies/my_custom_strategy.hpp"  // Add this line

## Step 3: Update Strategy Configuration

Add your strategy to `winter_strategies.conf`:

# winter_strategies.conf
# Format: strategy_id=strategy_name

1=StatArbitrage
2=MeanReversion
3=MyCustomStrategy
4=momentum_strategy
5=pairs_trading

## Step 4: Build and Test

Compile the framework with your new strategy:

make clean
make

Test your strategy:

# Run backtest with your custom strategy (ID 3)
./build/simulate --backtest 3 your_market_data.csv

# Run trade simulation
./build/simulate --trade 3 your_market_data.csv

## Step 5: Advanced Strategy Features

### Adding Parameters

Make your strategy configurable by adding parameters:

class MyCustomStrategy : public winter::strategy::StrategyBase {
private:
    // Configurable parameters
    double entry_threshold;
    double exit_threshold;
    int lookback_period;
    double max_position_pct;
    
public:
    MyCustomStrategy(const std::string& name = "MyCustomStrategy", 
                    double entry_thresh = 2.0,
                    double exit_thresh = 0.5,
                    int lookback = 20) 
        : StrategyBase(name), 
          entry_threshold(entry_thresh),
          exit_threshold(exit_thresh),
          lookback_period(lookback) {}

### Adding Risk Management

Implement position sizing and risk controls:

private:
    double calculate_position_size(const std::string& symbol, double price) {
        // Simple position sizing based on capital percentage
        const double CAPITAL = 5000000.0; // $5M
        return (CAPITAL * max_position_pct) / price;
    }
    
    bool check_risk_limits(const std::string& symbol, double price) {
        // Add your risk checks here
        // e.g., maximum exposure, correlation limits, etc.
        return true;
    }

### Adding Performance Tracking

Track strategy performance:

private:
    struct PerformanceMetrics {
        int total_trades = 0;
        int winning_trades = 0;
        double total_pnl = 0.0;
        double max_drawdown = 0.0;
    } performance;
    
    void update_performance(double trade_pnl) {
        performance.total_trades++;
        performance.total_pnl += trade_pnl;
        if (trade_pnl > 0) {
            performance.winning_trades++;
        }
        // Update other metrics...
    }

## Step 6: Strategy Templates

### Mean Reversion Template

class MeanReversionTemplate : public winter::strategy::StrategyBase {
    // Implement mean reversion logic
    // - Calculate moving averages
    // - Detect overbought/oversold conditions
    // - Generate contrarian signals
};

### Momentum Template

class MomentumTemplate : public winter::strategy::StrategyBase {
    // Implement momentum logic
    // - Calculate price momentum
    // - Detect trend breakouts
    // - Generate trend-following signals
};

### Pairs Trading Template

class PairsTradingTemplate : public winter::strategy::StrategyBase {
    // Implement pairs trading logic
    // - Calculate spread between correlated assets
    // - Detect spread divergence
    // - Generate market-neutral signals
};

## Step 7: Testing and Validation

### Unit Testing

Create tests for your strategy:

// tests/test_my_custom_strategy.cpp
#include "strategies/my_custom_strategy.hpp"
#include <cassert>

void test_signal_generation() {
    MyCustomStrategy strategy;
    winter::core::MarketData data;
    data.symbol = "TEST";
    data.price = 100.0;
    data.volume = 1000;
    
    auto signals = strategy.process_tick(data);
    // Add your assertions here
}

### Backtesting

Test with historical data:

# Test with different time periods
./build/simulate --backtest 3 data_2020.csv
./build/simulate --backtest 3 data_2021.csv
./build/simulate --backtest 3 data_2022.csv

### Parameter Optimization

Test different parameter combinations:

# Modify parameters in your strategy and test
# entry_threshold: 1.5, 2.0, 2.5
# exit_threshold: 0.3, 0.5, 0.7
# lookback_period: 10, 20, 30

## Common Patterns

### Signal Types

// Buy signal
signal.type = winter::core::SignalType::BUY;

// Sell signal  
signal.type = winter::core::SignalType::SELL;

// Exit signal (close position)
signal.type = winter::core::SignalType::EXIT;

### Signal Strength

// Strong signal (1.0)
signal.strength = 1.0;

// Moderate signal (0.5-0.8)
signal.strength = 0.7;

// Weak signal (0.1-0.4)
signal.strength = 0.3;

### Error Handling

std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
    std::vector<winter::core::Signal> signals;
    
    try {
        // Your strategy logic here
        
    } catch (const std::exception& e) {
        // Log error and return empty signals
        winter::utils::Logger::error() << "Strategy error: " << e.what() << winter::utils::Logger::endl;
        return signals;
    }
    
    return signals;
}

Your custom strategy is now ready to use with the Winter framework!

