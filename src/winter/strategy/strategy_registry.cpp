#include <winter/strategy/strategy_registry.hpp>
#include <winter/utils/logger.hpp>
#include <algorithm>

namespace winter::strategy {

std::vector<StrategyPtr> StrategyRegistry::strategies_;

void StrategyRegistry::register_strategy(StrategyPtr strategy) {
    // Check if strategy already exists
    auto it = std::find_if(strategies_.begin(), strategies_.end(),
        [&strategy](const StrategyPtr& s) {
            return s->name() == strategy->name();
        });
    
    if (it != strategies_.end()) {
        utils::Logger::warn() << "Strategy '" << strategy->name() 
                             << "' already registered, replacing" << utils::Logger::endl;
        *it = strategy;
    } else {
        strategies_.push_back(strategy);
        utils::Logger::info() << "Registered strategy: " << strategy->name() << utils::Logger::endl;
    }
}

// In src/winter/strategy/strategy_registry.cpp
void StrategyRegistry::clear() {
    // Use the correct method to access strategies
    get_all_strategies().clear();
}

void StrategyRegistry::unregister_strategy(const std::string& name) {
    auto it = std::find_if(strategies_.begin(), strategies_.end(),
        [&name](const StrategyPtr& s) {
            return s->name() == name;
        });
    
    if (it != strategies_.end()) {
        strategies_.erase(it);
        utils::Logger::info() << "Unregistered strategy: " << name << utils::Logger::endl;
    } else {
        utils::Logger::warn() << "Strategy '" << name << "' not found for unregistration" << utils::Logger::endl;
    }
}

StrategyPtr StrategyRegistry::get_strategy(const std::string& name) {
    auto it = std::find_if(strategies_.begin(), strategies_.end(),
        [&name](const StrategyPtr& s) {
            return s->name() == name;
        });
    
    if (it != strategies_.end()) {
        return *it;
    }
    
    utils::Logger::warn() << "Strategy '" << name << "' not found" << utils::Logger::endl;
    return nullptr;
}

std::vector<StrategyPtr> StrategyRegistry::get_all_strategies() {
    return strategies_;
}

// void StrategyRegistry::clear() {
//     strategies_.clear();
//     utils::Logger::info() << "Cleared all registered strategies" << utils::Logger::endl;
// }

} // namespace winter::strategy
