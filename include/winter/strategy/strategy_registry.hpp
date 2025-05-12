#pragma once
#include <winter/strategy/strategy_base.hpp>
#include <vector>
#include <string>

namespace winter::strategy {

class StrategyRegistry {
private:
    static std::vector<StrategyPtr> strategies_;
    
public:
    static void register_strategy(StrategyPtr strategy);
    static void unregister_strategy(const std::string& name);
    static StrategyPtr get_strategy(const std::string& name);
    static std::vector<StrategyPtr> get_all_strategies();
    static void clear();
    
    // Helper template for registering strategies
    template<typename T, typename... Args>
    static StrategyPtr create_and_register(Args&&... args) {
        auto strategy = std::make_shared<T>(std::forward<Args>(args)...);
        register_strategy(strategy);
        return strategy;
    }
};

} // namespace winter::strategy
