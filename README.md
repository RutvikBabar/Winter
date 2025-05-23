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
- [License](#license)

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

## License

MIT License. See `LICENSE` for details.
