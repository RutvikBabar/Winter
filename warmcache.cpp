#include <zmq.hpp>
#include <iostream>
#include <string>
#include <array>
#include <atomic>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <thread>
#include <csignal>
#include <unordered_map>
#include <shared_mutex>
#include <map>
#include <mutex>

#ifdef _WIN32
#include <windows.h>
void enable_virtual_terminal() {
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);
    SetConsoleMode(hOut, dwMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}
#else
void enable_virtual_terminal() {}
#endif

// ─── CONFIG ─────────────────────────────────────────
static constexpr const char* MODE = "TRADES";  // or "QUOTES"

constexpr double ENTRY_ZSCORE     = 6.0;
constexpr double MAX_ZSCORE       = 18.0;
constexpr double STOP_LOSS        = -20.0;
constexpr double TAKE_PROFIT      = 50.0;
constexpr size_t WINDOW_SIZE      = 500;
constexpr double STARTING_BALANCE = 100000.0;

// focus on these six pairs
constexpr std::array<std::pair<const char*, const char*>,6> monitored_pairs{{
    {"WM","RSG"},{"UAL","DAL"},{"V","MA"},
    {"MS","GS"},{"NVDA","AMD"},{"CVX","XOM"}
}};

// ─── TYPES ──────────────────────────────────────────
struct MarketData {
    double bid=0, ask=0;
    std::string ts;
    std::chrono::high_resolution_clock::time_point recv_t;
};

struct RollingWindow {
    std::array<double,WINDOW_SIZE> A{}, B{};
    std::atomic<size_t> idx{0};
    std::atomic<bool> full{false};
    double sum_a=0, sum_b=0, sum_ab=0, sum_b2=0, sum_a2=0;

    inline void add(double a, double b) noexcept {
        size_t i = idx % WINDOW_SIZE;
        if(full.load(std::memory_order_relaxed)) {
            double oa = A[i], ob = B[i];
            sum_a  -= oa;    sum_b  -= ob;
            sum_ab -= oa*ob; sum_b2 -= ob*ob;
            sum_a2 -= oa*oa;
        }
        A[i]=a; B[i]=b;
        sum_a  += a;    sum_b  += b;
        sum_ab += a*b;  sum_b2 += b*b;
        sum_a2 += a*a;
        ++idx;
        if(idx>=WINDOW_SIZE)
            full.store(true,std::memory_order_relaxed);
    }
    inline bool ready() const noexcept {
        return idx.load(std::memory_order_relaxed) > 0;
    }
    // dynamic N: if not yet full, N = current count; once full, N = WINDOW_SIZE
    inline double window_size() const noexcept {
        size_t c = idx.load(std::memory_order_relaxed);
        return double(c < WINDOW_SIZE ? c : WINDOW_SIZE);
    }
    inline double beta() const noexcept {
        double N = window_size();
        double cov = sum_ab - (sum_a*sum_b)/N;
        double varb= sum_b2 - (sum_b*sum_b)/N;
        return varb!=0 ? cov/varb : 1.0;
    }
    inline void stats(double &μ, double &σ) const noexcept {
        double N = window_size();
        double β = beta();
        double sum_sp  = sum_a - β*sum_b;
        μ = sum_sp/N;
        double sum_sp2 = sum_a2 - 2*β*sum_ab + β*β*sum_b2;
        double var     = sum_sp2/N - μ*μ;
        σ = var>0 ? std::sqrt(var) : 0.0;
    }
};

struct Trader {
    std::atomic<bool> in_pos{false};
    std::atomic<int>  qty{0};
    double entry_sp=0, entry_pa=0, entry_pb=0;
    std::string entry_ts;
    std::atomic<double> balance{STARTING_BALANCE};
    std::atomic<int>    won{0}, lost{0};
    double maxP=-1e9, maxL=1e9;
};

// ─── GLOBALS ────────────────────────────────────────
static std::unordered_map<std::string,MarketData> price_map;
static std::shared_mutex price_mtx;
static std::map<std::pair<std::string,std::string>,RollingWindow> RWs;
static std::map<std::pair<std::string,std::string>,Trader>        TRs;

static bool running = true;
void sigint_handler(int){ running=false; }

// ─── JSON EXTRACTORS ─────────────────────────────────
inline std::string jstr(const std::string &j, const char*k){
    auto p=j.find(k);
    if(p==std::string::npos) return {};
    p=j.find(':',p);
    auto a=j.find('"',p)+1;
    auto b=j.find('"',a);
    return j.substr(a,b-a);
}
inline double jnum(const std::string &j, const char*k){
    auto p=j.find(k);
    if(p==std::string::npos) return 0.0;
    p=j.find(':',p);
    auto a=j.find_first_of("0123456789.-",p);
    auto b=j.find_first_not_of("0123456789.eE+-",a);
    return std::stod(j.substr(a,b-a));
}

// ─── MAIN ───────────────────────────────────────────
int main(){
    enable_virtual_terminal();
    std::signal(SIGINT,sigint_handler);

    zmq::context_t ctx(1);
    zmq::socket_t sub(ctx,zmq::socket_type::sub);
    sub.set(zmq::sockopt::subscribe, "");
    sub.connect("tcp://127.0.0.1:5555");

    while(running){
        zmq::message_t msg;
        if(!sub.recv(msg,zmq::recv_flags::dontwait)){
            std::this_thread::sleep_for(std::chrono::microseconds(50));
            continue;
        }

        auto recv_t0 = std::chrono::high_resolution_clock::now();
        std::string raw((char*)msg.data(), msg.size());

        // parse
        std::string sym = jstr(raw,"Symbol");
        std::string ts  = jstr(raw,"Time");
        double    pr   = jnum(raw,"Price");
        double bid=0, ask=0;
        if(MODE[0]=='Q'){
            bid = jnum(raw,"Bid Price");
            ask = jnum(raw,"Ask Price");
        } else {
            bid = ask = pr;
        }
        if(sym.empty()||bid==0.0||ask==0.0) continue;

        // keep L1 hot
        volatile double _h = pr; (void)_h;

        // update price map
        {
            std::unique_lock lk(price_mtx);
            price_map[sym] = {bid,ask,ts,recv_t0};
        }

        // raw tick
        {
            double μs = std::chrono::duration<double,std::micro>(
                std::chrono::high_resolution_clock::now() - recv_t0
            ).count();
            std::cout
              <<'['<<ts<<"] "<<sym
              <<" | Price:"<<pr
              <<" | Proc:"<<std::fixed<<std::setprecision(1)<<μs<<"μs\n";
        }
        if(MODE[0]=='Q') continue;

        // process each pair unconditionally
        for(auto const &pp:monitored_pairs){
            auto A = pp.first, B = pp.second;
            MarketData mA, mB;
            {
                std::shared_lock lk(price_mtx);
                auto iA = price_map.find(A),
                     iB = price_map.find(B);
                if(iA==price_map.end()||iB==price_map.end())
                    continue;
                mA = iA->second;
                mB = iB->second;
            }

            auto ev0 = std::chrono::high_resolution_clock::now();
            double pA = (mA.bid+mA.ask)*0.5;
            double pB = (mB.bid+mB.ask)*0.5;

            auto &rw = RWs[{A,B}];
            rw.add(pA,pB);

            // compute Z immediately
            double μ, σ;
            rw.stats(μ, σ);
            double β      = rw.beta();
            double spread = pA - β*pB;
            double z      = σ>0 ? (spread - μ)/σ : 0.0;

            // trades only once we have at least one bar
            auto &tr = TRs[{A,B}];
            if(rw.ready()){
                // ENTRY
                if(!tr.in_pos.load() 
                   && fabs(z)>=ENTRY_ZSCORE 
                   && fabs(z)<=MAX_ZSCORE)
                {
                    int qty = int(0.1 * tr.balance.load() / (pA + pB));
                    if(qty>0){
                        tr.in_pos.store(true);
                        tr.qty.store(qty);
                        tr.entry_sp = spread;
                        tr.entry_pa= pA;  tr.entry_pb= pB;
                        tr.entry_ts = ts;
                        std::cout<<"\n\033[94m[ENTRY] "<<A<<"-"<<B<<"\n"
                                 <<"  TIME "<<ts<<"\n"
                                 <<"  A:"<<pA<<"  B:"<<pB<<"\n"
                                 <<"  QTY:"<<qty<<"\033[0m\n";
                    }
                }
                // EXIT
                else if(tr.in_pos.load()){
                    int   q = tr.qty.load();
                    double pnl = ((pA - tr.entry_pa) +
                                  (tr.entry_pb - pB)) * q;
                    bool tp = pnl >= TAKE_PROFIT;
                    bool sl = pnl <= STOP_LOSS;
                    bool cv = fabs(spread - tr.entry_sp)
                              < fabs(tr.entry_sp)*0.5;
                    if(tp||sl||cv){
                        tr.in_pos.store(false);
                        tr.balance.fetch_add(pnl);
                        if(pnl>=0) tr.won.fetch_add(1);
                        else        tr.lost.fetch_add(1);
                        tr.maxP = std::max(tr.maxP,pnl);
                        tr.maxL = std::min(tr.maxL,pnl);

                        const char* clr =
                          tp ? "\033[92m" :    // green
                          (sl ? "\033[91m" :   // red
                                "\033[93m");  // yellow
                        std::cout<<'\n'<<clr
                                 <<"[EXIT] "<<A<<"-"<<B<<"\n"
                                 <<"  TIME "<<ts<<"\n"
                                 <<"  PnL "<<(pnl>=0?"+":"")<<pnl<<"\n"
                                 <<"\033[0m\n";
                    }
                }
            }

            auto ev1 = std::chrono::high_resolution_clock::now();
            double ev_us = std::chrono::duration<double,std::micro>(
                             ev1 - ev0
                           ).count();

            // always print Z
            std::cout
              <<"   "<<A<<"/"<<B
              <<" | Z:"<<std::fixed<<std::setprecision(2)<<z
              <<" | Eval:"<<std::fixed<<std::setprecision(1)
              <<ev_us<<"μs\n";
        }
    }

    // ─── SHUTDOWN REPORT ──────────────────────────────
    std::cout<<"\n\033[93m=== SESSION END ===\033[0m\n\n";
    double totalPnL=0;
    int tw=0, tl=0;
    std::cout<<"\033[96m--- PER-PAIR PnL ---\033[0m\n";
    for(auto const &kv:TRs){
        auto const &pr = kv.first;
        auto const &t  = kv.second;
        double pnl = t.balance.load() - STARTING_BALANCE;
        totalPnL += pnl;
        tw += t.won.load();
        tl += t.lost.load();
        std::cout
          <<pr.first<<"-"<<pr.second
          <<" : "<<(pnl>=0?"+":"")
          <<std::fixed<<std::setprecision(2)<<pnl
          <<" | W:"<<t.won<<" L:"<<t.lost<<"\n";
    }
    int tot = tw+tl;
    double wr = tot?100.0*tw/tot:0.0;
    std::cout
      <<"\n\033[95m--- OVERALL ---\033[0m\n"
      <<"START $"<<STARTING_BALANCE<<"\n"
      <<"END   $"<<(STARTING_BALANCE+totalPnL)<<"\n"
      <<"PnL   "<<(totalPnL>=0?"+":"")<<totalPnL<<"\n"
      <<"TRADES "<<tot<<" W:"<<tw<<" L:"<<tl<<"\n"
      <<"WIN% "<<std::fixed<<std::setprecision(1)<<wr<<"%\n";

    return 0;
}
