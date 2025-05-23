#pragma once
#include <winter/strategy/strategy_base.hpp>
#include <winter/core/signal.hpp>
#include <winter/strategy/strategy_factory.hpp>
#include <winter/core/market_data.hpp>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <string>
#include <vector>
#include <utility>
#include <chrono>
#include <ctime>
#include <mutex>
#include <random>
#include <winter/utils/logger.hpp>
#include <thread>
#include <queue>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <iomanip>
#include <memory>

// Global map to store z-scores for each symbol
extern std::unordered_map<std::string, double> last_z_scores;

class StatisticalArbitrageStrategy : public winter::strategy::StrategyBase {
private:
    // OPTIMIZED PARALLEL PROCESSING with enhanced queue management
    const int MAX_THREADS = std::min(12, static_cast<int>(std::thread::hardware_concurrency()));
    std::vector<std::thread> worker_threads;
    std::vector<std::queue<std::shared_ptr<winter::core::MarketData>>> data_queues;
    std::unique_ptr<std::mutex[]> queue_mutexes;
    std::unique_ptr<std::condition_variable[]> queue_cvs;
    std::atomic<bool> running{true};
    std::atomic<int> active_workers{0};
    std::mutex signals_mutex;
    std::vector<winter::core::Signal> pending_signals;
    
    // ENHANCED QUEUE MANAGEMENT
    const size_t MAX_QUEUE_SIZE = 25000000; // Balanced queue size
    std::unique_ptr<std::atomic<size_t>[]> queue_sizes;
    std::atomic<size_t> dropped_messages{0};
    std::atomic<size_t> processed_messages{0};
    
    // OPTIMIZED BATCH PROCESSING
    const size_t BATCH_SIZE = 100; // Optimized batch size
    
    // Symbol to thread mapping for load balancing
    std::unordered_map<std::string, int> symbol_to_thread;
    std::mutex mapping_mutex;
    
    // RESTORED: Full symbol filtering with active pairs
    std::unordered_set<std::string> active_symbols;
    
    // RESTORED: Full 30 cointegrated pairs across diverse sectors
    std::vector<std::pair<std::string, std::string>> all_possible_pairs = {
        // Banking & Financial
        {"JPM", "BAC"},   // JP Morgan & Bank of America
        {"C", "WFC"},     // Citigroup & Wells Fargo
        {"GS", "MS"},     // Goldman Sachs & Morgan Stanley
        {"ITUB", "ITSA"}, // Itau Unibanco & Itausa
        
        // Technology
        {"AAPL", "MSFT"}, // Apple & Microsoft
        {"GOOGL", "FB"},  // Google & Facebook (Meta)
        {"AMD", "NVDA"},  // AMD & NVIDIA
        {"INTC", "TXN"},  // Intel & Texas Instruments
        
        // Oil & Gas
        {"XOM", "CVX"},   // Exxon Mobil & Chevron
        {"BP", "SHEL"},   // BP & Shell
        {"COP", "MRO"},   // ConocoPhillips & Marathon Oil
        {"SLB", "HAL"},   // Schlumberger & Halliburton
        
        // Mining & Materials
        {"VALE", "BHP"},  // Vale & BHP Billiton
        {"GOLD", "NEM"},  // Barrick Gold & Newmont
        {"RIO", "SCCO"},  // Rio Tinto & Southern Copper
        
        // Consumer Goods
        {"PG", "CL"},     // Procter & Gamble & Colgate-Palmolive
        {"KO", "PEP"},    // Coca-Cola & PepsiCo
        {"MO", "PM"},     // Altria & Philip Morris
        
        // Retail
        {"WMT", "TGT"},   // Walmart & Target
        {"HD", "LOW"},    // Home Depot & Lowe's
        
        // Pharmaceuticals
        {"JNJ", "PFE"},   // Johnson & Johnson & Pfizer
        {"MRK", "BMY"},   // Merck & Bristol-Myers Squibb
        {"ABBV", "LLY"},  // AbbVie & Eli Lilly
        
        // Telecommunications
        {"T", "VZ"},      // AT&T & Verizon
        {"TMUS", "VZ"},   // T-Mobile & Verizon
        
        // Automotive
        {"F", "GM"},      // Ford & General Motors
        {"TM", "NSANY"},  // Toyota & Nissan
        
        // ETFs (highly cointegrated)
        {"SPY", "IVV"},   // S&P 500 ETFs
        {"QQQ", "XLK"},   // Tech-heavy ETFs
        {"XLE", "VDE"}    // Energy ETFs
    };
    
    std::vector<std::pair<std::string, std::string>> active_pairs;
    
    // RESTORED: Advanced entry/exit rules with multiple timeframes
    double ENTRY_THRESHOLD = 1.2;      // Optimized threshold
    double ENTRY_CONFIRMATION = 0.08;  // Confirmation requirement
    double EXIT_THRESHOLD = 0.1;       // Mean reversion exit
    double PROFIT_TARGET_MULT = 0.25;  // Profit target multiplier
    double TRAILING_STOP_PCT = 0.85;   // Trailing stop percentage
    
    const double MAX_POSITION_PCT = 0.0015; // Position sizing
    const double CAPITAL = 5000000.0;
    
    // RESTORED: Multi-timeframe parameters
    static constexpr int SHORT_LOOKBACK = 8;   // Short-term lookback
    static constexpr int MEDIUM_LOOKBACK = 15; // Medium-term lookback
    static constexpr int LONG_LOOKBACK = 25;   // Long-term lookback
    
    // RESTORED: Time-based parameters
    const int MAX_HOLDING_PERIODS = 48; // Maximum holding period in hours
    const int MIN_HOLDING_PERIODS = 3;  // Minimum holding period
    
    // RESTORED: Advanced risk management
    const double STOP_LOSS_PCT = 0.012;  // Stop loss percentage
    const double MAX_SECTOR_ALLOCATION = 0.20; // Sector allocation limit
    
    // RESTORED: Enhanced cash management
    const double MIN_CASH_RESERVE_PCT = 0.30; // Minimum cash reserve
    const double EMERGENCY_CASH_LEVEL = 0.15; // Emergency cash level
    std::atomic<double> available_cash{CAPITAL};
    std::mutex cash_mutex;
    
    // RESTORED: Market making parameters
    bool market_making_enabled = true;
    double market_making_spread_threshold = 0.0008;
    
    // RESTORED: Enhanced logging control
    bool verbose_logging = true;
    int log_every_n_trades = 500; // More frequent logging
    std::atomic<int> trade_counter{0};
    
    struct PairData {
        PairData() = default;
        
        std::string symbol1;
        std::string symbol2;
        std::string sector;
        
        // RESTORED: Multi-timeframe spread history
        std::deque<double> spread_history_short;
        std::deque<double> spread_history_medium;
        std::deque<double> spread_history_long;
        
        int position1 = 0;
        int position2 = 0;
        
        // RESTORED: Advanced beta calculation
        double beta = 1.0;  // Dynamic hedge ratio
        double half_life = 0.0; // Mean reversion half-life
        double entry_price1 = 0.0;
        double entry_price2 = 0.0;
        
        // RESTORED: Enhanced exit strategy fields
        double entry_z_score = 0.0;
        double peak_profit = 0.0;
        double max_favorable_excursion = 0.0;
        double entry_time = 0.0;
        double prev_z_score = 0.0;
        
        // RESTORED: Multi-timeframe statistics
        double spread_mean_short = 0.0;
        double spread_std_short = 0.0;
        double spread_mean_medium = 0.0;
        double spread_std_medium = 0.0;
        double spread_mean_long = 0.0;
        double spread_std_long = 0.0;
        
        // RESTORED: Advanced performance tracking
        int signals_generated = 0;
        int signals_filled = 0;
        int trade_count = 0;
        double total_pnl = 0.0;
        double max_drawdown = 0.0;
        double current_position_value = 0.0;
        double sharpe_ratio = 1.0;
        std::deque<double> returns;
        
        // RESTORED: Cointegration tracking
        double cointegration_score = 0.0;
        double correlation_coefficient = 0.0;
        
        PairData(const std::string& s1, const std::string& s2, const std::string& sec = "Unknown") 
            : symbol1(s1), symbol2(s2), sector(sec) {}
            
        double get_fill_rate() const {
            return signals_generated > 0 ? 
                static_cast<double>(signals_filled) / signals_generated : 0.0;
        }
        
        double get_unrealized_pnl(const std::unordered_map<std::string, double>& prices) const {
            if (position1 == 0 && position2 == 0) return 0.0;
            
            if (prices.find(symbol1) == prices.end() || prices.find(symbol2) == prices.end()) {
                return 0.0;
            }
            
            double current_value1 = position1 * prices.at(symbol1);
            double current_value2 = position2 * prices.at(symbol2);
            double entry_value1 = position1 * entry_price1;
            double entry_value2 = position2 * entry_price2;
            
            return (current_value1 - entry_value1) + (current_value2 - entry_value2);
        }
        
        double get_position_value(const std::unordered_map<std::string, double>& prices) const {
            if (position1 == 0 && position2 == 0) return 0.0;
            
            if (prices.find(symbol1) == prices.end() || prices.find(symbol2) == prices.end()) {
                return 0.0;
            }
            
            return std::abs(position1 * prices.at(symbol1)) + std::abs(position2 * prices.at(symbol2));
        }
        
        double get_performance(const std::unordered_map<std::string, double>& prices) const {
            double pos_value = get_position_value(prices);
            if (pos_value <= 0) return 0.0;
            
            return get_unrealized_pnl(prices) / pos_value;
        }
        
        // RESTORED: Advanced Sharpe ratio calculation
        void update_sharpe_ratio() {
            if (returns.size() < 5) return;
            
            double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
            double mean = sum / returns.size();
            
            double sq_sum = 0.0;
            for (double r : returns) {
                sq_sum += (r - mean) * (r - mean);
            }
            
            double std_dev = std::sqrt(sq_sum / returns.size());
            if (std_dev > 0.0001) {
                sharpe_ratio = (mean / std_dev) * std::sqrt(252.0); // Annualized
            }
        }
        
        void add_return(double ret) {
            returns.push_back(ret);
            if (returns.size() > 30) { // Keep more history
                returns.pop_front();
            }
            update_sharpe_ratio();
        }
        
        // RESTORED: Dynamic beta calculation
        void update_beta(const std::deque<double>& price_history1, const std::deque<double>& price_history2) {
            if (price_history1.size() < MEDIUM_LOOKBACK || price_history2.size() < MEDIUM_LOOKBACK) {
                return;
            }
            
            // Calculate returns
            std::vector<double> returns1, returns2;
            for (size_t i = 1; i < MEDIUM_LOOKBACK && i < price_history1.size(); ++i) {
                returns1.push_back((price_history1[i] / price_history1[i-1]) - 1.0);
                returns2.push_back((price_history2[i] / price_history2[i-1]) - 1.0);
            }
            
            if (returns1.size() < 5) return;
            
            // Calculate beta using linear regression
            double sum_x = std::accumulate(returns2.begin(), returns2.end(), 0.0);
            double sum_y = std::accumulate(returns1.begin(), returns1.end(), 0.0);
            double mean_x = sum_x / returns2.size();
            double mean_y = sum_y / returns1.size();
            
            double numerator = 0.0, denominator = 0.0;
            for (size_t i = 0; i < returns1.size(); ++i) {
                numerator += (returns2[i] - mean_x) * (returns1[i] - mean_y);
                denominator += (returns2[i] - mean_x) * (returns2[i] - mean_x);
            }
            
            if (denominator > 0.0001) {
                beta = numerator / denominator;
                beta = std::max(0.5, std::min(2.0, beta)); // Clamp beta
            }
        }
        
        // RESTORED: Half-life calculation
        void calculate_half_life() {
            if (spread_history_medium.size() < MEDIUM_LOOKBACK) return;
            
            // Simple half-life estimation using AR(1) model
            std::vector<double> spreads(spread_history_medium.begin(), spread_history_medium.end());
            std::vector<double> lagged_spreads(spreads.begin(), spreads.end() - 1);
            spreads.erase(spreads.begin());
            
            if (spreads.size() < 5) return;
            
            // Calculate AR(1) coefficient
            double sum_x = std::accumulate(lagged_spreads.begin(), lagged_spreads.end(), 0.0);
            double sum_y = std::accumulate(spreads.begin(), spreads.end(), 0.0);
            double mean_x = sum_x / lagged_spreads.size();
            double mean_y = sum_y / spreads.size();
            
            double numerator = 0.0, denominator = 0.0;
            for (size_t i = 0; i < spreads.size(); ++i) {
                numerator += (lagged_spreads[i] - mean_x) * (spreads[i] - mean_y);
                denominator += (lagged_spreads[i] - mean_x) * (lagged_spreads[i] - mean_x);
            }
            
            if (denominator > 0.0001) {
                double ar_coeff = numerator / denominator;
                if (ar_coeff > 0 && ar_coeff < 1) {
                    half_life = -std::log(2.0) / std::log(ar_coeff);
                }
            }
        }
    };
    
    // RESTORED: Full data structures
    std::unordered_map<std::string, PairData> pair_data;
    std::mutex pair_data_mutex;
    
    std::unordered_map<std::string, double> latest_prices;
    std::mutex prices_mutex;
    
    // RESTORED: Per-thread price history
    std::vector<std::unordered_map<std::string, std::deque<double>>> thread_price_history;
    std::vector<std::unique_ptr<std::mutex>> history_mutexes;
    
    // RESTORED: Volatility tracking
    std::vector<std::unordered_map<std::string, double>> thread_volatility;
    std::vector<std::unique_ptr<std::mutex>> volatility_mutexes;
    double market_volatility = 0.015;
    
    // RESTORED: Sector allocation tracking
    std::unordered_map<std::string, double> sector_allocation;
    std::mutex sector_mutex;
    
    // RESTORED: Trading day tracking
    std::string current_day = "";
    bool unwinding_mode = false;
    std::mutex day_mutex;
    
    // RESTORED: Symbol tracking
    std::unordered_set<std::string> seen_symbols;
    int logged_symbols = 0;
    const int MAX_LOGGED_SYMBOLS = 30; // Increased logging
    std::mutex symbols_mutex;
    
    // RESTORED: Fill rate optimization
    double target_fill_rate = 0.30;
    double current_fill_rate = 0.0;
    std::atomic<int> total_signals{0};
    std::atomic<int> filled_signals{0};
    
    std::mt19937 rng;
    
    // RESTORED: Performance monitoring
    std::chrono::time_point<std::chrono::high_resolution_clock> last_stats_time;
    std::chrono::time_point<std::chrono::high_resolution_clock> last_cash_check_time;
    const int CASH_CHECK_INTERVAL_MS = 750; // Balanced interval
    
    // RESTORED: Adaptive throttling
    std::atomic<bool> throttling_enabled{true};
    std::atomic<int> throttle_level{0};
    const int MAX_THROTTLE_LEVEL = 3;

public:
    StatisticalArbitrageStrategy(const std::string& name = "StatArbitrage") : StrategyBase(name), rng(42) {
        // Use all 30 pairs
        active_pairs = all_possible_pairs;
        
        // Initialize enhanced thread structures
        data_queues.resize(MAX_THREADS);
        queue_mutexes = std::make_unique<std::mutex[]>(MAX_THREADS);
        queue_cvs = std::make_unique<std::condition_variable[]>(MAX_THREADS);
        queue_sizes = std::make_unique<std::atomic<size_t>[]>(MAX_THREADS);
        
        thread_price_history.resize(MAX_THREADS);
        history_mutexes.resize(MAX_THREADS);
        for (int i = 0; i < MAX_THREADS; i++) {
            history_mutexes[i] = std::make_unique<std::mutex>();
            queue_sizes[i] = 0;
        }
        
        thread_volatility.resize(MAX_THREADS);
        volatility_mutexes.resize(MAX_THREADS);
        for (int i = 0; i < MAX_THREADS; i++) {
            volatility_mutexes[i] = std::make_unique<std::mutex>();
        }
        
        // Build active symbols set
        for (const auto& pair : active_pairs) {
            active_symbols.insert(pair.first);
            active_symbols.insert(pair.second);
        }
        
        // Initialize pair data with enhanced features
        for (const auto& pair : active_pairs) {
            std::string pair_key = pair.first + "_" + pair.second;
            std::string sector = determine_sector(pair.first);
            
            std::lock_guard<std::mutex> lock(pair_data_mutex);
            pair_data[pair_key] = PairData(pair.first, pair.second, sector);
            
            assign_symbol_to_thread(pair.first);
            assign_symbol_to_thread(pair.second);
            
            winter::utils::Logger::info() << "Initialized pair: " << pair.first << "-" << pair.second 
                                      << " (" << sector << ")" << winter::utils::Logger::endl;
        }
        
        winter::utils::Logger::info() << "Trading " << active_pairs.size() 
                                  << " hardcoded cointegrated pairs" << winter::utils::Logger::endl;
        
        last_stats_time = std::chrono::high_resolution_clock::now();
        last_cash_check_time = std::chrono::high_resolution_clock::now();
        
        start_worker_threads();
    }
    
    ~StatisticalArbitrageStrategy() {
        stop_worker_threads();
    }
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        try {
            // Filter active symbols
            if (active_symbols.find(data.symbol) == active_symbols.end()) {
                return {};
            }
            
            auto data_ptr = std::make_shared<winter::core::MarketData>(data);
            int thread_id = get_thread_for_symbol(data.symbol);
            
            // Enhanced queue management
            bool enqueued = false;
            {
                std::lock_guard<std::mutex> lock(queue_mutexes[thread_id]);
                if (queue_sizes[thread_id].load() < MAX_QUEUE_SIZE) {
                    data_queues[thread_id].push(data_ptr);
                    queue_sizes[thread_id]++;
                    enqueued = true;
                }
            }
            
            if (enqueued) {
                queue_cvs[thread_id].notify_one();
            } else {
                dropped_messages++;
                if (dropped_messages % 25000 == 0) {
                    winter::utils::Logger::error() << "Market data queue full, dropping data for " 
                                               << data.symbol << winter::utils::Logger::endl;
                    log_performance_stats();
                }
            }
            
            // Periodic cash management
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cash_check_time).count();
            if (duration > CASH_CHECK_INTERVAL_MS) {
                check_and_free_capital();
                last_cash_check_time = now;
            }
            
            // Return pending signals
            std::vector<winter::core::Signal> signals;
            {
                std::lock_guard<std::mutex> lock(signals_mutex);
                if (!pending_signals.empty()) {
                    signals = std::move(pending_signals);
                    pending_signals.clear();
                }
            }
            
            return signals;
        } catch (...) {
            return {};
        }
    }
    
private:
    void assign_symbol_to_thread(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mapping_mutex);
        if (symbol_to_thread.find(symbol) == symbol_to_thread.end()) {
            size_t hash_val = std::hash<std::string>{}(symbol);
            symbol_to_thread[symbol] = hash_val % MAX_THREADS;
        }
    }
    
    int get_thread_for_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mapping_mutex);
        auto it = symbol_to_thread.find(symbol);
        if (it != symbol_to_thread.end()) {
            return it->second;
        }
        
        size_t hash_val = std::hash<std::string>{}(symbol);
        int thread_id = hash_val % MAX_THREADS;
        symbol_to_thread[symbol] = thread_id;
        return thread_id;
    }
    
    // RESTORED: Enhanced cash management
    void check_and_free_capital() {
        double total_allocated = 0.0;
        double avail_capital = CAPITAL;
        
        // Calculate sector allocations
        std::unordered_map<std::string, double> current_sector_allocation;
        
        {
            std::lock_guard<std::mutex> lock(pair_data_mutex);
            std::lock_guard<std::mutex> prices_lock(prices_mutex);
            
            for (auto& [pair_key, pd] : pair_data) {
                if (pd.position1 != 0 || pd.position2 != 0) {
                    double position_value = pd.get_position_value(latest_prices);
                    total_allocated += position_value;
                    pd.current_position_value = position_value;
                    
                    // Track sector allocation
                    current_sector_allocation[pd.sector] += position_value;
                }
            }
            
            avail_capital = CAPITAL - total_allocated;
            available_cash.store(avail_capital);
        }
        
        // Update sector allocation tracking
        {
            std::lock_guard<std::mutex> lock(sector_mutex);
            sector_allocation = current_sector_allocation;
        }
        
        double cash_pct = avail_capital / CAPITAL;
        
        // Emergency capital management
        if (cash_pct < EMERGENCY_CASH_LEVEL) {
            winter::utils::Logger::info() << "Emergency cash management triggered (" 
                                      << (cash_pct * 100.0) 
                                      << "% available)" << winter::utils::Logger::endl;
            
            // Close worst performing positions
            std::vector<std::pair<std::string, double>> position_performance;
            
            {
                std::lock_guard<std::mutex> lock(pair_data_mutex);
                std::lock_guard<std::mutex> prices_lock(prices_mutex);
                
                for (auto& [pair_key, pd] : pair_data) {
                    if (pd.position1 != 0 || pd.position2 != 0) {
                        double performance = pd.get_performance(latest_prices);
                        position_performance.push_back({pair_key, performance});
                    }
                }
            }
            
            std::sort(position_performance.begin(), position_performance.end(), 
                     [](const auto& a, const auto& b) { return a.second < b.second; });
            
            int positions_to_close = std::max(1, static_cast<int>(position_performance.size() * 0.25));
            
            for (int i = 0; i < positions_to_close && i < position_performance.size(); i++) {
                auto& pair_key = position_performance[i].first;
                
                std::lock_guard<std::mutex> lock(pair_data_mutex);
                auto pd_it = pair_data.find(pair_key);
                if (pd_it == pair_data.end()) continue;
                
                auto& pd = pd_it->second;
                
                std::lock_guard<std::mutex> prices_lock(prices_mutex);
                if (latest_prices.find(pd.symbol1) != latest_prices.end() && 
                    latest_prices.find(pd.symbol2) != latest_prices.end()) {
                    
                    auto exit_signals = generate_exit_signals(pd, latest_prices[pd.symbol1], latest_prices[pd.symbol2]);
                    
                    {
                        std::lock_guard<std::mutex> signals_lock(signals_mutex);
                        pending_signals.insert(pending_signals.end(), exit_signals.begin(), exit_signals.end());
                    }
                }
            }
        }
    }
    
    // RESTORED: Performance statistics logging
    void log_performance_stats() {
        auto now = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count();
        
        if (duration > 0) {
            double msgs_per_sec = processed_messages.load() / static_cast<double>(duration);
            double drop_rate = 0.0;
            size_t total_processed = processed_messages.load();
            size_t total_dropped = dropped_messages.load();
            
            if (total_processed + total_dropped > 0) {
                drop_rate = static_cast<double>(total_dropped) / 
                           static_cast<double>(total_processed + total_dropped) * 100.0;
            }
            
            // Calculate current fill rate
            int total_sigs = total_signals.load();
            int filled_sigs = filled_signals.load();
            current_fill_rate = total_sigs > 0 ? static_cast<double>(filled_sigs) / total_sigs : 0.0;
            
            winter::utils::Logger::info() << "Performance: " << msgs_per_sec << " msgs/sec, " 
                                      << drop_rate << "% drop rate, " 
                                      << (current_fill_rate * 100.0) << "% fill rate, "
                                      << active_workers.load() << "/" << MAX_THREADS << " workers, "
                                      << "Cash: " << (available_cash.load() / CAPITAL * 100.0) << "%"
                                      << winter::utils::Logger::endl;
            
            // Adaptive throttling adjustment
            if (drop_rate > 8.0 && throttle_level < MAX_THROTTLE_LEVEL) {
                throttle_level++;
                winter::utils::Logger::info() << "Increasing throttle level to " << throttle_level 
                                          << winter::utils::Logger::endl;
            } else if (drop_rate < 3.0 && throttle_level > 0) {
                throttle_level--;
                winter::utils::Logger::info() << "Decreasing throttle level to " << throttle_level 
                                          << winter::utils::Logger::endl;
            }
            
            processed_messages = 0;
            last_stats_time = now;
        }
    }
    
    void start_worker_threads() {
        running = true;
        for (int i = 0; i < MAX_THREADS; i++) {
            worker_threads.emplace_back([this, i]() {
                try {
                    worker_function(i);
                } catch (...) {
                    // Silent error handling
                }
            });
        }
        winter::utils::Logger::info() << "Started " << MAX_THREADS << " worker threads for parallel processing" 
                                  << winter::utils::Logger::endl;
    }
    
    void stop_worker_threads() {
        running = false;
        for (int i = 0; i < MAX_THREADS; i++) {
            queue_cvs[i].notify_all();
        }
        for (auto& thread : worker_threads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        worker_threads.clear();
    }
    
    void worker_function(int thread_id) {
        active_workers++;
        
        std::vector<std::shared_ptr<winter::core::MarketData>> batch_data;
        batch_data.reserve(BATCH_SIZE);
        std::vector<winter::core::Signal> batch_signals;
        
        while (running) {
            batch_data.clear();
            bool has_data = false;
            
            size_t current_queue_size = queue_sizes[thread_id].load();
            size_t items_to_collect = BATCH_SIZE;
            
            // Adaptive batch sizing
            if (current_queue_size > MAX_QUEUE_SIZE * 0.7) {
                items_to_collect = std::min(BATCH_SIZE * 2, current_queue_size);
            }
            
            {
                std::unique_lock<std::mutex> lock(queue_mutexes[thread_id]);
                queue_cvs[thread_id].wait_for(lock, std::chrono::milliseconds(2), [this, thread_id] {
                    return !data_queues[thread_id].empty() || !running; 
                });
                
                if (!running && data_queues[thread_id].empty()) {
                    break;
                }
                
                for (size_t i = 0; i < items_to_collect; ++i) {
                    if (data_queues[thread_id].empty()) break;
                    
                    batch_data.push_back(data_queues[thread_id].front());
                    data_queues[thread_id].pop();
                    queue_sizes[thread_id]--;
                    has_data = true;
                }
            }
            
            if (has_data) {
                try {
                    batch_signals.clear();
                    for (const auto& data_ptr : batch_data) {
                        if (!data_ptr) continue;
                        
                        auto signals = process_data_internal(*data_ptr, thread_id);
                        processed_messages++;
                        
                        if (!signals.empty()) {
                            batch_signals.insert(batch_signals.end(), signals.begin(), signals.end());
                        }
                    }
                    
                    if (!batch_signals.empty()) {
                        std::lock_guard<std::mutex> lock(signals_mutex);
                        pending_signals.insert(pending_signals.end(), batch_signals.begin(), batch_signals.end());
                    }
                } catch (...) {
                    // Silent error handling
                }
            }
            
            // Adaptive throttling
            if (throttling_enabled && throttle_level > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(throttle_level * 75));
            }
            
            if (!has_data) {
                std::this_thread::sleep_for(std::chrono::microseconds(250));
            }
        }
        
        active_workers--;
    }
    
    bool check_cash_for_position(double position_value) {
        double avail = available_cash.load();
        double current_cash_pct = avail / CAPITAL;
        
        if (current_cash_pct < MIN_CASH_RESERVE_PCT || avail < position_value) {
            return false;
        }
        
        double expected = avail;
        while (!available_cash.compare_exchange_weak(expected, expected - position_value)) {
            if (expected < position_value) {
                return false;
            }
        }
        
        return true;
    }
    
    // RESTORED: Enhanced sector allocation checking
    bool check_sector_allocation(const std::string& sector, double additional_allocation) {
        std::lock_guard<std::mutex> lock(sector_mutex);
        
        double current_allocation = 0.0;
        auto it = sector_allocation.find(sector);
        if (it != sector_allocation.end()) {
            current_allocation = it->second;
        }
        
        double new_allocation_pct = (current_allocation + additional_allocation) / CAPITAL;
        return new_allocation_pct <= MAX_SECTOR_ALLOCATION;
    }
    
    // RESTORED: Full data processing with all features
    std::vector<winter::core::Signal> process_data_internal(const winter::core::MarketData& data, int thread_id) {
        std::vector<winter::core::Signal> signals;
        
        try {
            // Enhanced symbol logging
            {
                std::lock_guard<std::mutex> lock(symbols_mutex);
                if (logged_symbols < MAX_LOGGED_SYMBOLS && seen_symbols.find(data.symbol) == seen_symbols.end()) {
                    seen_symbols.insert(data.symbol);
                    winter::utils::Logger::info() << "Found symbol in dataset: " << data.symbol << winter::utils::Logger::endl;
                    logged_symbols++;
                }
            }
            
            // Update price history and volatility
            update_price_history(data, thread_id);
            
            {
                std::lock_guard<std::mutex> lock(prices_mutex);
                latest_prices[data.symbol] = data.price;
            }
            
            // Day tracking for EOD management
            std::time_t time = data.timestamp / 1000000;
            std::tm* tm_info = std::localtime(&time);
            std::string day = std::to_string(tm_info->tm_mday) + 
                            std::to_string(tm_info->tm_mon) + 
                            std::to_string(tm_info->tm_year);
            
            {
                std::lock_guard<std::mutex> lock(day_mutex);
                if (day != current_day) {
                    if (!current_day.empty()) {
                        unwinding_mode = false;
                        std::lock_guard<std::mutex> sector_lock(sector_mutex);
                        sector_allocation.clear();
                    }
                    current_day = day;
                }
            }
            
            // Process pairs containing this symbol
            for (const auto& pair : active_pairs) {
                if (data.symbol == pair.first || data.symbol == pair.second) {
                    std::string pair_key = pair.first + "_" + pair.second;
                    
                    double price1 = 0.0, price2 = 0.0;
                    bool have_both_prices = false;
                    
                    {
                        std::lock_guard<std::mutex> lock(prices_mutex);
                        auto it1 = latest_prices.find(pair.first);
                        auto it2 = latest_prices.find(pair.second);
                        if (it1 != latest_prices.end() && it2 != latest_prices.end()) {
                            price1 = it1->second;
                            price2 = it2->second;
                            have_both_prices = true;
                        }
                    }
                    
                    if (have_both_prices) {
                        std::lock_guard<std::mutex> lock(pair_data_mutex);
                        
                        auto pd_it = pair_data.find(pair_key);
                        if (pd_it == pair_data.end()) {
                            continue;
                        }
                        
                        auto& pd = pd_it->second;
                        
                        // RESTORED: Advanced exit logic with multiple conditions
                        if (pd.position1 != 0 || pd.position2 != 0) {
                            double unrealized_pnl = pd.get_unrealized_pnl(latest_prices);
                            double position_value = pd.get_position_value(latest_prices);
                            
                            if (position_value > 0) {
                                double profit_pct = unrealized_pnl / position_value;
                                
                                // Update peak profit
                                if (profit_pct > pd.peak_profit) {
                                    pd.peak_profit = profit_pct;
                                }
                                
                                // Multiple exit conditions
                                bool stop_loss_hit = unrealized_pnl < -STOP_LOSS_PCT * position_value;
                                
                                // RESTORED: Trailing stop logic
                                bool trailing_stop_hit = pd.peak_profit > 0.01 && // Only after 1% profit
                                                        (pd.peak_profit - profit_pct) >= TRAILING_STOP_PCT * pd.peak_profit;
                                
                                // RESTORED: Time-based exit with minimum holding period
                                double holding_time_hours = (data.timestamp - pd.entry_time) / (3600.0 * 1000000.0);
                                bool time_based_exit = holding_time_hours > MAX_HOLDING_PERIODS;
                                bool min_holding_met = holding_time_hours >= MIN_HOLDING_PERIODS;
                                
                                if ((stop_loss_hit || (trailing_stop_hit && min_holding_met) || time_based_exit)) {
                                    auto stop_signals = generate_exit_signals(pd, price1, price2);
                                    signals.insert(signals.end(), stop_signals.begin(), stop_signals.end());
                                    
                                    std::string exit_reason = stop_loss_hit ? "Stop Loss" : 
                                                            trailing_stop_hit ? "Trailing Stop" : "Time-based Exit";
                                    
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "EXIT (" << exit_reason << "): " 
                                                              << (pd.position1 > 0 ? "SELL " : "BUY ") << pair.first 
                                                              << ", " 
                                                              << (pd.position2 > 0 ? "SELL " : "BUY ") << pair.second 
                                                              << " | Holding: " << holding_time_hours << "h"
                                                              << winter::utils::Logger::endl;
                                    }
                                    
                                    pd.add_return(profit_pct);
                                    continue;
                                }
                            }
                        }
                        
                        // Update beta dynamically
                        if (thread_price_history[thread_id].find(pair.first) != thread_price_history[thread_id].end() &&
                            thread_price_history[thread_id].find(pair.second) != thread_price_history[thread_id].end()) {
                            pd.update_beta(thread_price_history[thread_id][pair.first], 
                                          thread_price_history[thread_id][pair.second]);
                        }
                        
                        // Calculate spread using dynamic beta
                        double spread = price1 - pd.beta * price2;
                        
                        // RESTORED: Multi-timeframe spread history
                        update_spread_history(pd, spread);
                        
                        // Generate signals with multi-timeframe analysis
                        if (pd.spread_history_medium.size() >= MEDIUM_LOOKBACK) {
                            // RESTORED: Multi-timeframe statistics
                            calculate_spread_statistics(pd);
                            
                            // Calculate half-life periodically
                            if (pd.spread_history_medium.size() % 10 == 0) {
                                pd.calculate_half_life();
                            }
                            
                            // RESTORED: Multi-timeframe z-scores
                            double z_score_short = calculate_z_score(pd.spread_history_short, spread, 
                                                                   pd.spread_mean_short, pd.spread_std_short);
                            double z_score_medium = calculate_z_score(pd.spread_history_medium, spread, 
                                                                    pd.spread_mean_medium, pd.spread_std_medium);
                            double z_score_long = calculate_z_score(pd.spread_history_long, spread, 
                                                                  pd.spread_mean_long, pd.spread_std_long);
                            
                            // Store z-scores
                            last_z_scores[pair.first] = z_score_medium;
                            last_z_scores[pair.second] = z_score_medium;
                            
                            // RESTORED: Entry confirmation logic
                            bool entry_confirmed = false;
                            if (z_score_medium > ENTRY_THRESHOLD && z_score_medium < pd.prev_z_score) {
                                entry_confirmed = true;
                            } else if (z_score_medium < -ENTRY_THRESHOLD && z_score_medium > pd.prev_z_score) {
                                entry_confirmed = true;
                            }
                            
                            pd.prev_z_score = z_score_medium;
                            
                            // Update max favorable excursion
                            if (pd.position1 != 0) {
                                double z_score_movement = pd.position1 > 0 ? 
                                    pd.entry_z_score - z_score_medium :
                                    z_score_medium - pd.entry_z_score;
                                
                                if (z_score_movement > pd.max_favorable_excursion) {
                                    pd.max_favorable_excursion = z_score_movement;
                                }
                            }
                            
                            // Entry logic with enhanced conditions
                            if (pd.position1 == 0 && pd.position2 == 0) {
                                double current_cash_pct = available_cash.load() / CAPITAL;
                                if (current_cash_pct < MIN_CASH_RESERVE_PCT) {
                                    continue;
                                }
                                
                                // RESTORED: Multi-timeframe entry confirmation
                                bool strong_signal = (std::abs(z_score_short) > ENTRY_THRESHOLD * 0.8) &&
                                                   (std::abs(z_score_medium) > ENTRY_THRESHOLD) &&
                                                   (std::abs(z_score_long) > ENTRY_THRESHOLD * 0.6);
                                
                                if (z_score_medium > ENTRY_THRESHOLD && entry_confirmed && strong_signal) {
                                    // Check sector allocation
                                    int qty1 = calculate_position_size(pair.first, price1, z_score_medium, thread_id, pd);
                                    int qty2 = calculate_position_size(pair.second, price2, z_score_medium, thread_id, pd);
                                    
                                    double position_value = qty1 * price1 + qty2 * price2;
                                    
                                    if (!check_cash_for_position(position_value) || 
                                        !check_sector_allocation(pd.sector, position_value)) {
                                        continue;
                                    }
                                    
                                    // Create signals
                                    winter::core::Signal signal1;
                                    signal1.symbol = pair.first;
                                    signal1.type = winter::core::SignalType::SELL;
                                    signal1.price = price1;
                                    signal1.strength = 1.0;
                                    signals.push_back(signal1);
                                    
                                    winter::core::Signal signal2;
                                    signal2.symbol = pair.second;
                                    signal2.type = winter::core::SignalType::BUY;
                                    signal2.price = price2;
                                    signal2.strength = 1.0;
                                    signals.push_back(signal2);
                                    
                                    // Update position tracking
                                    pd.position1 = -qty1;
                                    pd.position2 = qty2;
                                    pd.entry_price1 = price1;
                                    pd.entry_price2 = price2;
                                    pd.entry_z_score = z_score_medium;
                                    pd.peak_profit = 0.0;
                                    pd.max_favorable_excursion = 0.0;
                                    pd.entry_time = static_cast<double>(data.timestamp);
                                    
                                    pd.signals_generated += 2;
                                    pd.signals_filled += 2;
                                    pd.trade_count++;
                                    total_signals += 2;
                                    filled_signals += 2;
                                    
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "ENTRY: SELL " << pair.first << ", BUY " << pair.second 
                                                              << " | Z-score: " << z_score_medium 
                                                              << " | Beta: " << pd.beta << winter::utils::Logger::endl;
                                    }
                                }
                                else if (z_score_medium < -ENTRY_THRESHOLD && entry_confirmed && strong_signal) {
                                    // Similar logic for long spread entry
                                    int qty1 = calculate_position_size(pair.first, price1, -z_score_medium, thread_id, pd);
                                    int qty2 = calculate_position_size(pair.second, price2, -z_score_medium, thread_id, pd);
                                    
                                    double position_value = qty1 * price1 + qty2 * price2;
                                    
                                    if (!check_cash_for_position(position_value) || 
                                        !check_sector_allocation(pd.sector, position_value)) {
                                        continue;
                                    }
                                    
                                    winter::core::Signal signal1;
                                    signal1.symbol = pair.first;
                                    signal1.type = winter::core::SignalType::BUY;
                                    signal1.price = price1;
                                    signal1.strength = 1.0;
                                    signals.push_back(signal1);
                                    
                                    winter::core::Signal signal2;
                                    signal2.symbol = pair.second;
                                    signal2.type = winter::core::SignalType::SELL;
                                    signal2.price = price2;
                                    signal2.strength = 1.0;
                                    signals.push_back(signal2);
                                    
                                    pd.position1 = qty1;
                                    pd.position2 = -qty2;
                                    pd.entry_price1 = price1;
                                    pd.entry_price2 = price2;
                                    pd.entry_z_score = z_score_medium;
                                    pd.peak_profit = 0.0;
                                    pd.max_favorable_excursion = 0.0;
                                    pd.entry_time = static_cast<double>(data.timestamp);
                                    
                                    pd.signals_generated += 2;
                                    pd.signals_filled += 2;
                                    pd.trade_count++;
                                    total_signals += 2;
                                    filled_signals += 2;
                                    
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "ENTRY: BUY " << pair.first << ", SELL " << pair.second 
                                                              << " | Z-score: " << z_score_medium 
                                                              << " | Beta: " << pd.beta << winter::utils::Logger::endl;
                                    }
                                }
                            }
                            else {
                                // RESTORED: Enhanced exit conditions
                                bool mean_reversion_exit = (pd.position1 > 0 && z_score_medium > -EXIT_THRESHOLD) ||
                                                        (pd.position1 < 0 && z_score_medium < EXIT_THRESHOLD);
                                
                                bool profit_target_exit = pd.max_favorable_excursion > 0 && 
                                                        (pd.max_favorable_excursion * PROFIT_TARGET_MULT) <= 
                                                        std::abs(pd.entry_z_score - z_score_medium);
                                
                                // RESTORED: Multi-timeframe exit confirmation
                                bool multi_timeframe_exit = mean_reversion_exit && 
                                                          (std::abs(z_score_short) < EXIT_THRESHOLD * 1.5);
                                
                                if (multi_timeframe_exit || profit_target_exit) {
                                    auto exit_signals = generate_exit_signals(pd, price1, price2);
                                    signals.insert(signals.end(), exit_signals.begin(), exit_signals.end());
                                    
                                    std::string exit_reason = profit_target_exit ? "Profit Target" : "Mean Reversion";
                                    
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "EXIT (" << exit_reason << "): " 
                                                              << (pd.position1 > 0 ? "SELL " : "BUY ") << pair.first 
                                                              << ", " 
                                                              << (pd.position2 > 0 ? "SELL " : "BUY ") << pair.second 
                                                              << " | Z-score: " << z_score_medium << winter::utils::Logger::endl;
                                    }
                                    
                                    pd.signals_generated += 2;
                                    pd.signals_filled += 2;
                                    total_signals += 2;
                                    filled_signals += 2;
                                    
                                    double profit_pct = pd.get_unrealized_pnl(latest_prices) / pd.get_position_value(latest_prices);
                                    pd.add_return(profit_pct);
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (...) {
            // Silent error handling
        }
        
        return signals;
    }
    
    // RESTORED: Enhanced price history management
    void update_price_history(const winter::core::MarketData& data, int thread_id) {
        std::lock_guard<std::mutex> lock(*history_mutexes[thread_id]);
        
        if (thread_price_history[thread_id].find(data.symbol) == thread_price_history[thread_id].end()) {
            thread_price_history[thread_id][data.symbol] = std::deque<double>();
        }
        
        thread_price_history[thread_id][data.symbol].push_back(data.price);
        
        const size_t max_history = LONG_LOOKBACK * 3; // Keep more history
        if (thread_price_history[thread_id][data.symbol].size() > max_history) {
            thread_price_history[thread_id][data.symbol].pop_front();
        }
        
        // Update volatility with more sophisticated calculation
        if (thread_price_history[thread_id][data.symbol].size() >= 15) {
            std::lock_guard<std::mutex> vol_lock(*volatility_mutexes[thread_id]);
            thread_volatility[thread_id][data.symbol] = calculate_volatility(thread_price_history[thread_id][data.symbol]);
        }
    }
    
    // RESTORED: Advanced volatility calculation
    double calculate_volatility(const std::deque<double>& prices) {
        if (prices.size() < 2) return 0.0;
        
        std::vector<double> returns;
        returns.reserve(prices.size() - 1);
        for (size_t i = 1; i < prices.size(); ++i) {
            returns.push_back(std::log(prices[i] / prices[i-1]));
        }
        
        double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
        double mean = sum / returns.size();
        
        double sq_sum = 0.0;
        for (double r : returns) {
            sq_sum += (r - mean) * (r - mean);
        }
        
        double std_dev = std::sqrt(sq_sum / (returns.size() - 1)); // Sample standard deviation
        return std_dev * std::sqrt(252.0); // Annualized
    }
    
    // RESTORED: Multi-timeframe spread history
    void update_spread_history(PairData& pd, double spread) {
        pd.spread_history_short.push_back(spread);
        if (pd.spread_history_short.size() > SHORT_LOOKBACK) {
            pd.spread_history_short.pop_front();
        }
        
        pd.spread_history_medium.push_back(spread);
        if (pd.spread_history_medium.size() > MEDIUM_LOOKBACK) {
            pd.spread_history_medium.pop_front();
        }
        
        pd.spread_history_long.push_back(spread);
        if (pd.spread_history_long.size() > LONG_LOOKBACK) {
            pd.spread_history_long.pop_front();
        }
    }
    
    // RESTORED: Multi-timeframe statistics calculation
    void calculate_spread_statistics(PairData& pd) {
        // Short-term statistics
        if (pd.spread_history_short.size() >= SHORT_LOOKBACK) {
            pd.spread_mean_short = std::accumulate(pd.spread_history_short.begin(), 
                                                 pd.spread_history_short.end(), 0.0) / 
                                  pd.spread_history_short.size();
            
            double sum_sq = 0.0;
            for (double s : pd.spread_history_short) {
                double diff = s - pd.spread_mean_short;
                sum_sq += diff * diff;
            }
            pd.spread_std_short = std::sqrt(sum_sq / pd.spread_history_short.size());
        }
        
        // Medium-term statistics
        if (pd.spread_history_medium.size() >= MEDIUM_LOOKBACK) {
            pd.spread_mean_medium = std::accumulate(pd.spread_history_medium.begin(), 
                                                  pd.spread_history_medium.end(), 0.0) / 
                                   pd.spread_history_medium.size();
            
            double sum_sq = 0.0;
            for (double s : pd.spread_history_medium) {
                double diff = s - pd.spread_mean_medium;
                sum_sq += diff * diff;
            }
            pd.spread_std_medium = std::sqrt(sum_sq / pd.spread_history_medium.size());
        }
        
        // Long-term statistics
        if (pd.spread_history_long.size() >= LONG_LOOKBACK) {
            pd.spread_mean_long = std::accumulate(pd.spread_history_long.begin(), 
                                                pd.spread_history_long.end(), 0.0) / 
                                 pd.spread_history_long.size();
            
            double sum_sq = 0.0;
            for (double s : pd.spread_history_long) {
                double diff = s - pd.spread_mean_long;
                sum_sq += diff * diff;
            }
            pd.spread_std_long = std::sqrt(sum_sq / pd.spread_history_long.size());
        }
    }
    
    double calculate_z_score(const std::deque<double>& history, double current_value, 
                            double mean, double std_dev) {
        if (history.size() < 2 || std_dev < 0.0001) return 0.0;
        return (current_value - mean) / std_dev;
    }
    
    // RESTORED: Advanced position sizing with multiple factors
    int calculate_position_size(const std::string& symbol, double price, double z_score, int thread_id, const PairData& pd) {
        // Get historical volatility
        double vol = 0.015; // Default
        {
            std::lock_guard<std::mutex> lock(*volatility_mutexes[thread_id]);
            auto it = thread_volatility[thread_id].find(symbol);
            if (it != thread_volatility[thread_id].end()) {
                vol = it->second;
            }
        }
        
        // Volatility adjustment
        double vol_factor = std::min(2.0, 0.25 / std::max(0.03, vol));
        
        // Z-score scaling
        double z_score_factor = std::min(2.0, 0.7 + std::pow(std::abs(z_score) / ENTRY_THRESHOLD, 0.6));
        
        // Sharpe ratio scaling
        double sharpe_factor = std::max(0.4, std::min(1.8, pd.sharpe_ratio / 1.5));
        
        // Half-life adjustment
        double half_life_factor = 1.0;
        if (pd.half_life > 0 && pd.half_life < 100) {
            half_life_factor = std::min(1.5, 10.0 / pd.half_life);
        }
        
        // Market volatility adjustment
        double market_vol_factor = std::min(1.5, 0.02 / std::max(0.005, market_volatility));
        
        return std::max(1, static_cast<int>((CAPITAL * MAX_POSITION_PCT * 
                                           vol_factor * z_score_factor * sharpe_factor * 
                                           half_life_factor * market_vol_factor) / price));
    }
    
    std::vector<winter::core::Signal> generate_exit_signals(PairData& pd, double price1, double price2) {
        std::vector<winter::core::Signal> signals;
        
        if (pd.position1 != 0) {
            winter::core::Signal signal1;
            signal1.symbol = pd.symbol1;
            signal1.type = pd.position1 > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
            signal1.price = price1;
            signal1.strength = 1.0;
            signals.push_back(signal1);
        }
        
        if (pd.position2 != 0) {
            winter::core::Signal signal2;
            signal2.symbol = pd.symbol2;
            signal2.type = pd.position2 > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
            signal2.price = price2;
            signal2.strength = 1.0;
            signals.push_back(signal2);
        }
        
        // Free up capital
        double position_value = pd.get_position_value(latest_prices);
        available_cash.fetch_add(position_value);
        
        // Reset positions
        pd.position1 = 0;
        pd.position2 = 0;
        pd.peak_profit = 0.0;
        pd.max_favorable_excursion = 0.0;
        
        return signals;
    }
    
    // RESTORED
std::string determine_sector(const std::string& symbol) {
    // Simple sector determination based on first letter
    if (symbol.empty()) return "Unknown";
    
    char first_char = symbol[0];
    
    if (first_char == 'A') return "Technology";
    if (first_char == 'B') return "Financial";
    if (first_char == 'C') return "Consumer";
    if (first_char == 'D') return "Industrial";
    if (first_char == 'E') return "Energy";
    if (first_char == 'F') return "Automotive";
    if (first_char == 'G') return "Technology";
    if (first_char == 'H') return "Healthcare";
    if (first_char == 'I') return "Technology";
    if (first_char == 'J') return "Healthcare";
    if (first_char == 'K') return "Consumer";
    if (first_char == 'L') return "Financial";
    if (first_char == 'M') return "Healthcare";
    if (first_char == 'N') return "Materials";
    if (first_char == 'O') return "Energy";
    if (first_char == 'P') return "Consumer";
    if (first_char == 'Q') return "Technology";
    if (first_char == 'R') return "Financial";
    if (first_char == 'S') return "Technology";
    if (first_char == 'T') return "Telecommunications";
    if (first_char == 'U') return "Utilities";
    if (first_char == 'V') return "Financial";
    if (first_char == 'W') return "Consumer";
    if (first_char == 'X') return "ETF";
    if (first_char == 'Y') return "Technology";
    if (first_char == 'Z') return "Financial";
    
    return "Unknown";
}
};
// Register the strategy with the factory
namespace {
    bool stat_arb_registered = []() {
        winter::strategy::StrategyFactory::register_type<StatisticalArbitrageStrategy>("StatArbitrage");
        return true;
    }();
}
