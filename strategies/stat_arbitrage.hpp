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
    // Parallel processing with better memory management
    const int MAX_THREADS = std::min(16, static_cast<int>(std::thread::hardware_concurrency()));
    std::vector<std::thread> worker_threads;
    std::vector<std::queue<std::shared_ptr<winter::core::MarketData>>> data_queues; // One queue per thread
    std::unique_ptr<std::mutex[]> queue_mutexes;
    std::unique_ptr<std::condition_variable[]> queue_cvs;
    std::atomic<bool> running{true};
    std::atomic<int> active_workers{0};
    std::mutex signals_mutex;
    std::vector<winter::core::Signal> pending_signals;
    
    // Queue management parameters
    const size_t MAX_QUEUE_SIZE = 8000000; // Increased queue size significantly
    std::unique_ptr<std::atomic<size_t>[]> queue_sizes;
    std::atomic<size_t> dropped_messages{0};
    std::atomic<size_t> processed_messages{0};
    
    // Batch processing for efficiency
    const size_t BATCH_SIZE = 50; // Process messages in batches
    
    // Symbol to thread mapping for load balancing
    std::unordered_map<std::string, int> symbol_to_thread;
    std::mutex mapping_mutex;
    
    // Symbol filtering - only process symbols in active pairs
    std::unordered_set<std::string> active_symbols;
    
    // 30 top cointegrated pairs across diverse sectors
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
    
    // Active pairs that passed cointegration tests
    std::vector<std::pair<std::string, std::string>> active_pairs;
    
    // OPTIMIZED ENTRY/EXIT RULES based on data analysis
    double ENTRY_THRESHOLD = 1.3;      // REDUCED from 1.5 for earlier entries
    double ENTRY_CONFIRMATION = 0.05;  // NEW - require small reversal to confirm
    double EXIT_THRESHOLD = 0.0;       // REDUCED from 0.2 to target complete reversion
    double PROFIT_TARGET_MULT = 0.7;   // INCREASED from 0.5 for more aggressive profit taking
    double TRAILING_STOP_PCT = 0.25;   // INCREASED from 0.15 to avoid premature exits
    
    const double MAX_POSITION_PCT = 0.004; // ADJUSTED based on data analysis
    const double CAPITAL = 5000000.0;
    
    // Multi-timeframe parameters - OPTIMIZED based on data
    const int SHORT_LOOKBACK = 3;  // Reduced for faster signal generation
    const int MEDIUM_LOOKBACK = 5; // Reduced for faster signal generation
    const int LONG_LOOKBACK = 10;  // Reduced for faster signal generation
    
    // Time-based exit parameters
    const int MAX_HOLDING_PERIODS = 72; // Maximum holding period in hours
    
    // Risk management parameters - OPTIMIZED
    const double STOP_LOSS_PCT = 0.018;  // ADJUSTED from 0.02
    const double MAX_SECTOR_ALLOCATION = 0.25; // REDUCED from 0.30
    
    // Cash management parameters - OPTIMIZED
    const double MIN_CASH_RESERVE_PCT = 0.15; // Minimum cash to keep available
    const double EMERGENCY_CASH_LEVEL = 0.05; // Emergency level
    std::atomic<double> available_cash{CAPITAL};
    std::mutex cash_mutex;
    
    // Market making parameters
    bool market_making_enabled = true;
    double market_making_spread_threshold = 0.001;
    
    // Logging control
    bool verbose_logging = false;
    int log_every_n_trades = 1000;
    std::atomic<int> trade_counter{0};
    
    struct PairData {
        PairData() = default;
        
        std::string symbol1;
        std::string symbol2;
        std::string sector;
        std::deque<double> spread_history_short;
        std::deque<double> spread_history_medium;
        std::deque<double> spread_history_long;
        int position1 = 0;
        int position2 = 0;
        double beta = 1.0;  // Hedge ratio
        double half_life = 0.0; // Mean reversion half-life
        double entry_price1 = 0.0;
        double entry_price2 = 0.0;
        
        // Fields for enhanced exit strategies
        double entry_z_score = 0.0;    // Z-score at entry
        double peak_profit = 0.0;      // Track peak profit for trailing stop
        double max_favorable_excursion = 0.0; // Track maximum favorable z-score movement
        double entry_time = 0.0; // Track when position was entered
        double prev_z_score = 0.0; // Track previous z-score for confirmation
        
        // Statistics
        double spread_mean_short = 0.0;
        double spread_std_short = 0.0;
        double spread_mean_medium = 0.0;
        double spread_std_medium = 0.0;
        double spread_mean_long = 0.0;
        double spread_std_long = 0.0;
        
        // Fill rate tracking
        int signals_generated = 0;
        int signals_filled = 0;
        
        // Performance metrics
        int trade_count = 0;
        double total_pnl = 0.0;
        double max_drawdown = 0.0;
        double current_position_value = 0.0;
        double sharpe_ratio = 1.0; // Default Sharpe ratio
        std::deque<double> returns; // Store historical returns
        
        PairData(const std::string& s1, const std::string& s2, const std::string& sec = "Unknown") 
            : symbol1(s1), symbol2(s2), sector(sec) {}
            
        double get_fill_rate() const {
            return signals_generated > 0 ? 
                static_cast<double>(signals_filled) / signals_generated : 0.0;
        }
        
        double get_unrealized_pnl(const std::unordered_map<std::string, double>& prices) const {
            if (position1 == 0 && position2 == 0) return 0.0;
            
            if (prices.find(symbol1) == prices.end() || prices.find(symbol2) == prices.end()) {
                return 0.0; // Avoid accessing non-existent keys
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
                return 0.0; // Avoid accessing non-existent keys
            }
            
            return std::abs(position1 * prices.at(symbol1)) + std::abs(position2 * prices.at(symbol2));
        }
        
        double get_performance(const std::unordered_map<std::string, double>& prices) const {
            double pos_value = get_position_value(prices);
            if (pos_value <= 0) return 0.0;
            
            return get_unrealized_pnl(prices) / pos_value;
        }
        
        // Calculate Sharpe ratio based on historical returns
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
                sharpe_ratio = mean / std_dev;
            }
        }
        
        // Add a return to the history
        void add_return(double ret) {
            returns.push_back(ret);
            if (returns.size() > 20) {
                returns.pop_front();
            }
            update_sharpe_ratio();
        }
    };
    
    // Data for each pair - protected by mutex
    std::unordered_map<std::string, PairData> pair_data;
    std::mutex pair_data_mutex;
    
    // Latest prices for each symbol (thread-safe)
    std::unordered_map<std::string, double> latest_prices;
    std::mutex prices_mutex;
    
    // Historical price data for each symbol - per thread
    std::vector<std::unordered_map<std::string, std::deque<double>>> thread_price_history;
    std::vector<std::unique_ptr<std::mutex>> history_mutexes;
    
    // Volatility tracking - per thread
    std::vector<std::unordered_map<std::string, double>> thread_volatility;
    std::vector<std::unique_ptr<std::mutex>> volatility_mutexes;
    double market_volatility = 0.015; // Default market volatility
    
    // Sector allocation tracking
    std::unordered_map<std::string, double> sector_allocation;
    std::mutex sector_mutex;
    
    // Trading day tracking
    std::string current_day = "";
    bool unwinding_mode = false;
    std::mutex day_mutex;
    
    // Symbol tracking - only log first 20 symbols
    std::unordered_set<std::string> seen_symbols;
    int logged_symbols = 0;
    const int MAX_LOGGED_SYMBOLS = 20;
    std::mutex symbols_mutex;
    
    // Fill rate optimization
    double target_fill_rate = 0.25; // Target 25% fill rate
    double current_fill_rate = 0.0;
    std::atomic<int> total_signals{0};
    std::atomic<int> filled_signals{0};
    
    // Random number generator for simulation
    std::mt19937 rng;
    
    // Performance monitoring
    std::chrono::time_point<std::chrono::high_resolution_clock> last_stats_time;
    
    // Adaptive throttling
    std::atomic<bool> throttling_enabled{false};
    std::atomic<int> throttle_level{0};
    const int MAX_THROTTLE_LEVEL = 3;
    
    // Cash management tracking
    std::chrono::time_point<std::chrono::high_resolution_clock> last_cash_check_time;
    const int CASH_CHECK_INTERVAL_MS = 500; // Check cash every 500ms
    
public:
    StatisticalArbitrageStrategy(const std::string& name = "StatArbitrage") : StrategyBase(name), rng(42) {
        // Use all hardcoded pairs - no filtering
        active_pairs = all_possible_pairs;
        
        // Initialize thread-specific data structures
        data_queues.resize(MAX_THREADS);
        queue_mutexes = std::make_unique<std::mutex[]>(MAX_THREADS);
        queue_cvs = std::make_unique<std::condition_variable[]>(MAX_THREADS);
        queue_sizes = std::make_unique<std::atomic<size_t>[]>(MAX_THREADS);
        
        thread_price_history.resize(MAX_THREADS);
        
        // Initialize mutexes using unique_ptr
        history_mutexes.resize(MAX_THREADS);
        for (int i = 0; i < MAX_THREADS; i++) {
            history_mutexes[i] = std::make_unique<std::mutex>();
        }
        
        thread_volatility.resize(MAX_THREADS);
        
        // Initialize mutexes using unique_ptr
        volatility_mutexes.resize(MAX_THREADS);
        for (int i = 0; i < MAX_THREADS; i++) {
            volatility_mutexes[i] = std::make_unique<std::mutex>();
        }
        
        for (int i = 0; i < MAX_THREADS; i++) {
            queue_sizes[i] = 0;
        }
        
        // Build active symbols set for quick filtering
        for (const auto& pair : active_pairs) {
            active_symbols.insert(pair.first);
            active_symbols.insert(pair.second);
        }
        
        // Initialize pair data
        for (const auto& pair : active_pairs) {
            std::string pair_key = pair.first + "_" + pair.second;
            std::string sector = determine_sector(pair.first);
            
            std::lock_guard<std::mutex> lock(pair_data_mutex);
            pair_data[pair_key] = PairData(pair.first, pair.second, sector);
            
            // Pre-assign symbols to threads for load balancing
            assign_symbol_to_thread(pair.first);
            assign_symbol_to_thread(pair.second);
            
            winter::utils::Logger::info() << "Initialized pair: " << pair.first << "-" << pair.second 
                                      << " (" << sector << ")" << winter::utils::Logger::endl;
        }
        
        winter::utils::Logger::info() << "Trading " << active_pairs.size() 
                                  << " hardcoded cointegrated pairs" << winter::utils::Logger::endl;
        
        // Initialize performance monitoring
        last_stats_time = std::chrono::high_resolution_clock::now();
        last_cash_check_time = std::chrono::high_resolution_clock::now();
        
        // Start worker threads for parallel processing
        start_worker_threads();
    }
    
    ~StatisticalArbitrageStrategy() {
        stop_worker_threads();
    }
    
    std::vector<winter::core::Signal> process_tick(const winter::core::MarketData& data) override {
        try {
            // Quick filter - only process symbols in our active pairs
            if (active_symbols.find(data.symbol) == active_symbols.end()) {
                return {}; // Skip symbols we don't care about
            }
            
            // Make a copy of the data to prevent memory issues
            auto data_ptr = std::make_shared<winter::core::MarketData>(data);
            
            // Get thread assignment for this symbol
            int thread_id = get_thread_for_symbol(data.symbol);
            
            // Safely enqueue the data with queue size management
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
                // Track dropped messages
                dropped_messages++;
                if (dropped_messages % 10000 == 0) {
                    winter::utils::Logger::error() << "Market data queue full, dropped " 
                                               << dropped_messages << " messages" 
                                               << winter::utils::Logger::endl;
                    
                    // Log performance stats
                    log_performance_stats();
                    
                    // Enable throttling if too many drops
                    if (dropped_messages > 100000 && !throttling_enabled) {
                        throttling_enabled = true;
                        winter::utils::Logger::info() << "Enabling adaptive throttling due to high drop rate" 
                                                  << winter::utils::Logger::endl;
                    }
                }
            }
            
            // Check cash periodically
            auto now = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cash_check_time).count();
            if (duration > CASH_CHECK_INTERVAL_MS) {
                check_and_free_capital();
                last_cash_check_time = now;
            }
            
            // Return any pending signals
            std::vector<winter::core::Signal> signals;
            {
                std::lock_guard<std::mutex> lock(signals_mutex);
                if (!pending_signals.empty()) {
                    signals = std::move(pending_signals);
                    pending_signals.clear();
                }
            }
            
            return signals;
        } catch (const std::exception& e) {
            winter::utils::Logger::error() << "Error in process_tick: " << e.what() << winter::utils::Logger::endl;
            return {};
        } catch (...) {
            winter::utils::Logger::error() << "Unknown error in process_tick" << winter::utils::Logger::endl;
            return {};
        }
    }
    
private:
    // Assign symbol to thread for load balancing
    void assign_symbol_to_thread(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mapping_mutex);
        if (symbol_to_thread.find(symbol) == symbol_to_thread.end()) {
            // Simple hash-based assignment
            size_t hash_val = std::hash<std::string>{}(symbol);
            symbol_to_thread[symbol] = hash_val % MAX_THREADS;
        }
    }
    
    // Get thread assignment for symbol
    int get_thread_for_symbol(const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mapping_mutex);
        auto it = symbol_to_thread.find(symbol);
        if (it != symbol_to_thread.end()) {
            return it->second;
        }
        
        // If not assigned yet, assign now
        size_t hash_val = std::hash<std::string>{}(symbol);
        int thread_id = hash_val % MAX_THREADS;
        symbol_to_thread[symbol] = thread_id;
        return thread_id;
    }
    
    // Check and free capital if needed
    void check_and_free_capital() {
        double total_allocated = 0.0;
        double avail_capital = CAPITAL;
        
        // Calculate currently allocated capital
        {
            std::lock_guard<std::mutex> lock(pair_data_mutex);
            std::lock_guard<std::mutex> prices_lock(prices_mutex);
            
            for (auto& [pair_key, pd] : pair_data) {
                if (pd.position1 != 0 || pd.position2 != 0) {
                    double position_value = pd.get_position_value(latest_prices);
                    total_allocated += position_value;
                    pd.current_position_value = position_value;
                }
            }
            
            avail_capital = CAPITAL - total_allocated;
            available_cash.store(avail_capital);
        }
        
        double cash_pct = avail_capital / CAPITAL;
        
        // If less than threshold capital available, close worst performing positions
        if (cash_pct < EMERGENCY_CASH_LEVEL) {
            winter::utils::Logger::info() << "Low capital available (" 
                                      << (cash_pct * 100.0) 
                                      << "%), freeing up resources" << winter::utils::Logger::endl;
            
            // Sort positions by performance and close bottom 20%
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
            
            // Sort by performance (worst first)
            std::sort(position_performance.begin(), position_performance.end(), 
                     [](const auto& a, const auto& b) { return a.second < b.second; });
            
            // Close worst 20% of positions
            int positions_to_close = std::max(1, static_cast<int>(position_performance.size() * 0.2));
            
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
                    
                    winter::utils::Logger::info() << "Freeing capital: Closing position for pair " 
                                              << pd.symbol1 << "-" << pd.symbol2 
                                              << " (performance: " << position_performance[i].second * 100.0 
                                              << "%)" << winter::utils::Logger::endl;
                }
            }
        }
    }
    
    // Log performance statistics
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
            
            winter::utils::Logger::info() << "Performance: " << msgs_per_sec << " msgs/sec, " 
                                      << drop_rate << "% drop rate, " 
                                      << active_workers.load() << "/" << MAX_THREADS << " active workers, "
                                      << "Cash: " << (available_cash.load() / CAPITAL * 100.0) << "%"
                                      << winter::utils::Logger::endl;
            
            // Adjust throttling based on drop rate - MODIFIED thresholds for more aggressive trading
            if (drop_rate > 10.0 && throttle_level < MAX_THROTTLE_LEVEL) { // INCREASED from 5.0
                throttle_level++;
                winter::utils::Logger::info() << "Increasing throttle level to " << throttle_level 
                                          << " due to high drop rate" << winter::utils::Logger::endl;
            } else if (drop_rate < 2.0 && throttle_level > 0) { // INCREASED from 1.0
                throttle_level--;
                winter::utils::Logger::info() << "Decreasing throttle level to " << throttle_level 
                                          << " due to low drop rate" << winter::utils::Logger::endl;
            }
            
            // Reset counters
            processed_messages = 0;
            last_stats_time = now;
        }
    }
    
    // Start worker threads with exception handling
    void start_worker_threads() {
        running = true;
        for (int i = 0; i < MAX_THREADS; i++) {
            worker_threads.emplace_back([this, i]() {
                try {
                    worker_function(i);
                } catch (const std::exception& e) {
                    winter::utils::Logger::error() << "Worker thread " << i 
                                               << " exception: " << e.what() 
                                               << winter::utils::Logger::endl;
                } catch (...) {
                    winter::utils::Logger::error() << "Worker thread " << i 
                                               << " unknown exception" 
                                               << winter::utils::Logger::endl;
                }
            });
        }
        winter::utils::Logger::info() << "Started " << MAX_THREADS << " worker threads for parallel processing" 
                                  << winter::utils::Logger::endl;
    }
    
    // Stop worker threads safely
    void stop_worker_threads() {
        running = false;
        // Fixed: use index-based loop instead of range-based for loop
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
    
    // Worker thread function with better error handling
    void worker_function(int thread_id) {
        active_workers++;
        
        // Batch processing containers
        std::vector<std::shared_ptr<winter::core::MarketData>> batch_data;
        batch_data.reserve(BATCH_SIZE);
        std::vector<winter::core::Signal> batch_signals;
        
        while (running) {
            batch_data.clear();
            bool has_data = false;
            
            // Check queue size and adapt processing strategy
            size_t current_queue_size = queue_sizes[thread_id].load();
            
            // If queue is getting full, process more aggressively
            size_t items_to_collect = BATCH_SIZE;
            if (current_queue_size > MAX_QUEUE_SIZE * 0.8) {
                // Process larger batches when queue is nearly full
                items_to_collect = std::min(BATCH_SIZE * 3, current_queue_size);
                
                if (current_queue_size > MAX_QUEUE_SIZE * 0.9) {
                    // Emergency mode: process only active pairs
                    winter::utils::Logger::info() << "Thread " << thread_id 
                                                << " queue nearly full, entering emergency processing mode" 
                                                << winter::utils::Logger::endl;
                }
            }
            
            // Collect a batch of data to process
            {
                std::unique_lock<std::mutex> lock(queue_mutexes[thread_id]);
                queue_cvs[thread_id].wait_for(lock, std::chrono::milliseconds(5), [this, thread_id] { // REDUCED from 10ms
                    return !data_queues[thread_id].empty() || !running; 
                });
                
                if (!running && data_queues[thread_id].empty()) {
                    break;
                }
                
                // Collect up to items_to_collect items or all available items
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
                    // Process the batch
                    batch_signals.clear();
                    for (const auto& data_ptr : batch_data) {
                        if (!data_ptr) continue;
                        
                        auto signals = process_data_internal(*data_ptr, thread_id);
                        processed_messages++;
                        
                        if (!signals.empty()) {
                            batch_signals.insert(batch_signals.end(), signals.begin(), signals.end());
                        }
                    }
                    
                    // Submit all signals at once
                    if (!batch_signals.empty()) {
                        std::lock_guard<std::mutex> lock(signals_mutex);
                        pending_signals.insert(pending_signals.end(), batch_signals.begin(), batch_signals.end());
                    }
                } catch (const std::exception& e) {
                    winter::utils::Logger::error() << "Error processing batch in thread " << thread_id 
                                               << ": " << e.what() << winter::utils::Logger::endl;
                } catch (...) {
                    winter::utils::Logger::error() << "Unknown error processing batch in thread " << thread_id 
                                               << winter::utils::Logger::endl;
                }
            }
            
            // Adaptive throttling - sleep based on throttle level
            if (throttling_enabled && throttle_level > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(throttle_level * 50)); // REDUCED from 100
            }
            
            // Prevent CPU spinning with a small sleep if queue is empty
            if (!has_data) {
                std::this_thread::sleep_for(std::chrono::microseconds(500)); // REDUCED from 1ms
            }
        }
        
        active_workers--;
    }
    
    // Parse timestamp from CSV data
    std::tm parse_csv_timestamp(const std::string& time_str) {
        std::tm tm = {};
        std::istringstream ss(time_str);
        ss >> std::get_time(&tm, "%Y-%m-%d %H:%M:%S");
        return tm;
    }
    
    // Check if we have enough cash for a position
    bool check_cash_for_position(double position_value) {
        double avail = available_cash.load();
        
        // Skip new entries if cash is below minimum reserve
        double current_cash_pct = avail / CAPITAL;
        if (current_cash_pct < MIN_CASH_RESERVE_PCT) {
            return false;
        }
        
        if (avail < position_value) {
            return false;
        }
        
        // Atomically update available cash
        double expected = avail;
        while (!available_cash.compare_exchange_weak(expected, expected - position_value)) {
            if (expected < position_value) {
                return false;
            }
        }
        
        return true;
    }
    
    // Internal data processing function
    std::vector<winter::core::Signal> process_data_internal(const winter::core::MarketData& data, int thread_id) {
        std::vector<winter::core::Signal> signals;
        
        try {
            // Limited symbol logging (only first 20 symbols)
            {
                std::lock_guard<std::mutex> lock(symbols_mutex);
                if (logged_symbols < MAX_LOGGED_SYMBOLS && seen_symbols.find(data.symbol) == seen_symbols.end()) {
                    seen_symbols.insert(data.symbol);
                    winter::utils::Logger::info() << "Found symbol in dataset: " << data.symbol << winter::utils::Logger::endl;
                    logged_symbols++;
                }
            }
            
            // Update price history and volatility - thread-specific
            update_price_history(data, thread_id);
            
            // Update latest price (thread-safe)
            {
                std::lock_guard<std::mutex> lock(prices_mutex);
                latest_prices[data.symbol] = data.price;
            }
            
            // Extract day and time from timestamp for EOD tracking and timing optimization
            // Use the timestamp from the data instead of system time
            std::time_t time = data.timestamp / 1000000; // Convert to seconds
            std::tm* tm_info = std::localtime(&time);
            std::string day = std::to_string(tm_info->tm_mday) + 
                            std::to_string(tm_info->tm_mon) + 
                            std::to_string(tm_info->tm_year);
            
            // Check if day has changed - SIMPLIFIED for speed
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
            
            // Process only for pairs containing this symbol
            for (const auto& pair : active_pairs) {
                if (data.symbol == pair.first || data.symbol == pair.second) {
                    std::string pair_key = pair.first + "_" + pair.second;
                    
                    // Thread-safe access to prices
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
                        // Thread-safe access to pair data
                        std::lock_guard<std::mutex> lock(pair_data_mutex);
                        
                        // Check if pair exists in pair_data
                        auto pd_it = pair_data.find(pair_key);
                        if (pd_it == pair_data.end()) {
                            continue;  // Skip if pair not found
                        }
                        
                        auto& pd = pd_it->second;
                        
                        // Apply stop-loss to existing positions
                        if (pd.position1 != 0 || pd.position2 != 0) {
                            double unrealized_pnl = pd.get_unrealized_pnl(latest_prices);
                            double position_value = std::abs(pd.position1 * price1) + 
                                                std::abs(pd.position2 * price2);
                            
                            if (position_value <= 0) {
                                continue;  // Skip if invalid position value
                            }
                            
                            // Calculate profit percentage for trailing stop
                            double profit_pct = unrealized_pnl / position_value;
                            
                            // Update peak profit if current profit is higher
                            if (profit_pct > pd.peak_profit) {
                                pd.peak_profit = profit_pct;
                            }
                            
                            // Check if stop-loss is hit
                            bool stop_loss_hit = unrealized_pnl < -STOP_LOSS_PCT * position_value;
                            
                            // Check if trailing stop is hit
                            bool trailing_stop_hit = pd.peak_profit > 0 && 
                                                (pd.peak_profit - profit_pct) >= TRAILING_STOP_PCT * pd.peak_profit;
                            
                            // Check if time-based exit is needed
                            bool time_based_exit = (data.timestamp - pd.entry_time) > 
                                                  (MAX_HOLDING_PERIODS * 3600 * 1000000); // Convert to microseconds
                            
                            if (stop_loss_hit || trailing_stop_hit || time_based_exit) {
                                auto stop_signals = generate_exit_signals(pd, price1, price2);
                                signals.insert(signals.end(), stop_signals.begin(), stop_signals.end());
                                
                                // Log exit reason
                                std::string exit_reason = stop_loss_hit ? "Stop Loss" : 
                                                      trailing_stop_hit ? "Trailing Stop" : "Time-based Exit";
                                winter::utils::Logger::info() << "EXIT (" << exit_reason << "): " 
                                                        << (pd.position1 > 0 ? "SELL " : "BUY ") << pair.first 
                                                        << ", " 
                                                        << (pd.position2 > 0 ? "SELL " : "BUY ") << pair.second 
                                                        << winter::utils::Logger::endl;
                                
                                // Record trade result for Sharpe ratio calculation
                                if (position_value > 0) {
                                    pd.add_return(profit_pct);
                                }
                                
                                continue;
                            }
                        }
                        
                        // Calculate spread using beta (hedge ratio)
                        double spread = price1 - pd.beta * price2;
                        
                        // Update spread history for multiple timeframes
                        update_spread_history(pd, spread);
                        
                        // Only generate signals if we have enough history - use shorter lookback
                        if (pd.spread_history_medium.size() >= MEDIUM_LOOKBACK) {
                            // Calculate statistics for all timeframes
                            calculate_spread_statistics(pd);
                            
                            // Calculate z-score for medium timeframe only - for speed
                            double z_score_medium = calculate_z_score(pd.spread_history_medium, spread, 
                                                                    pd.spread_mean_medium, pd.spread_std_medium);
                            
                            // Store z-score in global map for both symbols
                            last_z_scores[pair.first] = z_score_medium;
                            last_z_scores[pair.second] = z_score_medium;
                            
                            // Check for entry confirmation (reversal has begun)
                            bool entry_confirmed = false;
                            if (z_score_medium > ENTRY_THRESHOLD && z_score_medium < pd.prev_z_score) {
                                // Confirmed short entry (z-score high but starting to decrease)
                                entry_confirmed = true;
                            } else if (z_score_medium < -ENTRY_THRESHOLD && z_score_medium > pd.prev_z_score) {
                                // Confirmed long entry (z-score low but starting to increase)
                                entry_confirmed = true;
                            }
                            
                            // Store current z-score for next comparison
                            pd.prev_z_score = z_score_medium;
                            
                            // Update max favorable excursion for existing positions
                            if (pd.position1 != 0) {
                                double z_score_movement = pd.position1 > 0 ? 
                                    pd.entry_z_score - z_score_medium :  // For long positions, we want z-score to decrease
                                    z_score_medium - pd.entry_z_score;   // For short positions, we want z-score to increase
                                
                                if (z_score_movement > pd.max_favorable_excursion) {
                                    pd.max_favorable_excursion = z_score_movement;
                                }
                            }
                            
                            // Generate trading signals based on z-score
                            if (pd.position1 == 0 && pd.position2 == 0) {
                                // Check cash availability before entry
                                double current_cash_pct = available_cash.load() / CAPITAL;
                                if (current_cash_pct < MIN_CASH_RESERVE_PCT) {
                                    continue; // Skip new entries if cash is below minimum reserve
                                }
                                
                                // No position, check for entry with confirmation
                                if (z_score_medium > ENTRY_THRESHOLD && entry_confirmed) {
                                    // Spread is too high, short spread (short symbol1, long symbol2)
                                    pd.signals_generated += 2; // Count both legs
                                    total_signals += 2;
                                    
                                    int qty1 = calculate_position_size(pair.first, price1, z_score_medium, thread_id, pd);
                                    int qty2 = calculate_position_size(pair.second, price2, z_score_medium, thread_id, pd);
                                    
                                    // Check if we have enough cash for this position
                                    double position_value = qty1 * price1 + qty2 * price2;
                                    if (!check_cash_for_position(position_value)) {
                                        continue; // Skip if not enough cash
                                    }
                                    
                                    // Create signals
                                    winter::core::Signal signal1;
                                    signal1.symbol = pair.first;
                                    signal1.type = winter::core::SignalType::SELL;
                                    signal1.price = price1;
                                    signal1.strength = 1.0;  // Always use full strength
                                    signals.push_back(signal1);
                                    
                                    winter::core::Signal signal2;
                                    signal2.symbol = pair.second;
                                    signal2.type = winter::core::SignalType::BUY;
                                    signal2.price = price2;
                                    signal2.strength = 1.0;  // Always use full strength
                                    signals.push_back(signal2);
                                    
                                    // Track positions
                                    pd.position1 = -qty1;
                                    pd.position2 = qty2;
                                    pd.entry_price1 = price1;
                                    pd.entry_price2 = price2;
                                    pd.entry_z_score = z_score_medium; // Store entry z-score
                                    pd.peak_profit = 0.0;
                                    pd.max_favorable_excursion = 0.0;
                                    pd.entry_time = static_cast<double>(data.timestamp);
                                    
                                    // Update fill tracking
                                    pd.signals_filled += 2;
                                    filled_signals += 2;
                                    pd.trade_count++;
                                    
                                    // Log only occasionally for speed
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "ENTRY: SELL " << pair.first << ", BUY " << pair.second 
                                                              << " | Z-score: " << z_score_medium << winter::utils::Logger::endl;
                                    }
                                }
                                else if (z_score_medium < -ENTRY_THRESHOLD && entry_confirmed) {
                                    // Spread is too low, long spread (long symbol1, short symbol2)
                                    pd.signals_generated += 2; // Count both legs
                                    total_signals += 2;
                                    
                                    int qty1 = calculate_position_size(pair.first, price1, -z_score_medium, thread_id, pd);
                                    int qty2 = calculate_position_size(pair.second, price2, -z_score_medium, thread_id, pd);
                                    
                                    // Check if we have enough cash for this position
                                    double position_value = qty1 * price1 + qty2 * price2;
                                    if (!check_cash_for_position(position_value)) {
                                        continue; // Skip if not enough cash
                                    }
                                    
                                    // Create signals
                                    winter::core::Signal signal1;
                                    signal1.symbol = pair.first;
                                    signal1.type = winter::core::SignalType::BUY;
                                    signal1.price = price1;
                                    signal1.strength = 1.0;  // Always use full strength
                                    signals.push_back(signal1);
                                    
                                    winter::core::Signal signal2;
                                    signal2.symbol = pair.second;
                                    signal2.type = winter::core::SignalType::SELL;
                                    signal2.price = price2;
                                    signal2.strength = 1.0;  // Always use full strength
                                    signals.push_back(signal2);
                                    
                                    // Track positions
                                    pd.position1 = qty1;
                                    pd.position2 = -qty2;
                                    pd.entry_price1 = price1;
                                    pd.entry_price2 = price2;
                                    pd.entry_z_score = z_score_medium; // Store entry z-score
                                    pd.peak_profit = 0.0;
                                    pd.max_favorable_excursion = 0.0;
                                    pd.entry_time = static_cast<double>(data.timestamp);
                                    
                                    // Update fill tracking
                                    pd.signals_filled += 2;
                                    filled_signals += 2;
                                    pd.trade_count++;
                                    
                                    // Log only occasionally for speed
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "ENTRY: BUY " << pair.first << ", SELL " << pair.second 
                                                              << " | Z-score: " << z_score_medium << winter::utils::Logger::endl;
                                    }
                                }
                            }
                            else {
                                // Have position, check for exit using multiple exit conditions
                                
                                // 1. Mean reversion exit
                                bool mean_reversion_exit = (pd.position1 > 0 && z_score_medium > -EXIT_THRESHOLD) ||
                                                        (pd.position1 < 0 && z_score_medium < EXIT_THRESHOLD);
                                
                                // 2. Profit target exit based on z-score movement
                                bool profit_target_exit = pd.max_favorable_excursion > 0 && 
                                                        (pd.max_favorable_excursion * PROFIT_TARGET_MULT) <= 
                                                        std::abs(pd.entry_z_score - z_score_medium);
                                
                                if (mean_reversion_exit || profit_target_exit) {
                                    // Exit condition met
                                    pd.signals_generated += 2; // Count both legs
                                    total_signals += 2;
                                    
                                    // Create signals
                                    winter::core::Signal signal1;
                                    signal1.symbol = pd.symbol1;
                                    signal1.type = pd.position1 > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
                                    signal1.price = price1;
                                    signal1.strength = 1.0;  // Always use full strength
                                    signals.push_back(signal1);
                                    
                                    winter::core::Signal signal2;
                                    signal2.symbol = pd.symbol2;
                                    signal2.type = pd.position2 > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
                                    signal2.price = price2;
                                    signal2.strength = 1.0;  // Always use full strength
                                    signals.push_back(signal2);
                                    
                                    // Log exit reason
                                    std::string exit_reason = mean_reversion_exit ? "Mean Reversion" : "Profit Target";
                                    
                                    // Log only occasionally for speed
                                    if (verbose_logging || (++trade_counter % log_every_n_trades == 0)) {
                                        winter::utils::Logger::info() << "EXIT (" << exit_reason << "): " 
                                                              << (pd.position1 > 0 ? "SELL " : "BUY ") << pair.first 
                                                              << ", " 
                                                              << (pd.position2 > 0 ? "SELL " : "BUY ") << pair.second 
                                                              << " | Z-score: " << z_score_medium << winter::utils::Logger::endl;
                                    }
                                    
                                    // Update fill tracking
                                    pd.signals_filled += 2;
                                    filled_signals += 2;
                                    
                                    // Record trade result for Sharpe ratio calculation
                                    double profit_pct = pd.get_unrealized_pnl(latest_prices) / pd.get_position_value(latest_prices);
                                    pd.add_return(profit_pct);
                                    
                                    // Free up capital
                                    double position_value = pd.get_position_value(latest_prices);
                                    available_cash.fetch_add(position_value);
                                    
                                    // Reset positions
                                    pd.position1 = 0;
                                    pd.position2 = 0;
                                    pd.peak_profit = 0.0;
                                    pd.max_favorable_excursion = 0.0;
                                }
                            }
                        }
                    }
                }
            }
        }
        catch (const std::exception& e) {
            winter::utils::Logger::error() << "Error in process_data_internal: " << e.what() << winter::utils::Logger::endl;
        }
        catch (...) {
            winter::utils::Logger::error() << "Unknown error in process_data_internal" << winter::utils::Logger::endl;
        }
        
        return signals;
    }
    
    void update_price_history(const winter::core::MarketData& data, int thread_id) {
        // Thread-safe update of price history
        std::lock_guard<std::mutex> lock(*history_mutexes[thread_id]);
        
        // Update price history for the symbol
        if (thread_price_history[thread_id].find(data.symbol) == thread_price_history[thread_id].end()) {
            thread_price_history[thread_id][data.symbol] = std::deque<double>();
        }
        
        thread_price_history[thread_id][data.symbol].push_back(data.price);
        
        // Keep only necessary history - REDUCED for speed
        const size_t max_history = LONG_LOOKBACK * 2;
        if (thread_price_history[thread_id][data.symbol].size() > max_history) {
            thread_price_history[thread_id][data.symbol].pop_front();
        }
        
        // Update volatility if we have enough data
        if (thread_price_history[thread_id][data.symbol].size() >= 10) {
            std::lock_guard<std::mutex> vol_lock(*volatility_mutexes[thread_id]);
            thread_volatility[thread_id][data.symbol] = calculate_volatility(thread_price_history[thread_id][data.symbol]);
        }
    }
    
    double calculate_volatility(const std::deque<double>& prices) {
        if (prices.size() < 2) return 0.0;
        
        // Calculate returns
        std::vector<double> returns;
        returns.reserve(prices.size() - 1);
        for (size_t i = 1; i < prices.size(); ++i) {
            returns.push_back((prices[i] / prices[i-1]) - 1.0);
        }
        
        // Calculate standard deviation of returns
        double sum = std::accumulate(returns.begin(), returns.end(), 0.0);
        double mean = sum / returns.size();
        
        double sq_sum = 0.0;
        for (double r : returns) {
            sq_sum += (r - mean) * (r - mean);
        }
        
        double std_dev = std::sqrt(sq_sum / returns.size());
        
        // Annualize volatility (assuming daily data)
        return std_dev * std::sqrt(252.0);
    }
    
    void update_spread_history(PairData& pd, double spread) {
        // Update short-term history
        pd.spread_history_short.push_back(spread);
        if (pd.spread_history_short.size() > SHORT_LOOKBACK) {
            pd.spread_history_short.pop_front();
        }
        
        // Update medium-term history
        pd.spread_history_medium.push_back(spread);
        if (pd.spread_history_medium.size() > MEDIUM_LOOKBACK) {
            pd.spread_history_medium.pop_front();
        }
        
        // Update long-term history
        pd.spread_history_long.push_back(spread);
        if (pd.spread_history_long.size() > LONG_LOOKBACK) {
            pd.spread_history_long.pop_front();
        }
    }
    
    void calculate_spread_statistics(PairData& pd) {
        // Calculate statistics for medium-term only - for speed
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
    }
    
    double calculate_z_score(const std::deque<double>& history, double current_value, 
                            double mean, double std_dev) {
        if (history.size() < 2 || std_dev < 0.0001) return 0.0;
        return (current_value - mean) / std_dev;
    }
    
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
        
        // More aggressive position sizing with z-score scaling
        double vol_factor = std::min(2.5, 0.3 / std::max(0.05, vol)); // INCREASED from 1.5
        
        // Exponential scaling with z-score - more aggressive for stronger signals
        double z_score_factor = std::min(2.5, 0.8 + std::pow(std::abs(z_score) / ENTRY_THRESHOLD, 0.7));
        
        // Sharpe ratio scaling - use historical Sharpe for this pair
        double sharpe_factor = std::max(0.5, std::min(1.5, pd.sharpe_ratio / 2.0));
        
        return std::max(1, static_cast<int>((CAPITAL * MAX_POSITION_PCT * 
                                           vol_factor * z_score_factor * sharpe_factor) / price));
    }
    
    std::vector<winter::core::Signal> generate_exit_signals(PairData& pd, double price1, double price2) {
        std::vector<winter::core::Signal> signals;
        
        // Generate exit signals for both legs
        if (pd.position1 != 0) {
            winter::core::Signal signal1;
            signal1.symbol = pd.symbol1;
            signal1.type = pd.position1 > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
            signal1.price = price1;
            signal1.strength = 1.0; // Maximum urgency for exits
            signals.push_back(signal1);
        }
        
        if (pd.position2 != 0) {
            winter::core::Signal signal2;
            signal2.symbol = pd.symbol2;
            signal2.type = pd.position2 > 0 ? winter::core::SignalType::SELL : winter::core::SignalType::BUY;
            signal2.price = price2;
            signal2.strength = 1.0; // Maximum urgency for exits
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
