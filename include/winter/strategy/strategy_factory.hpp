// include/winter/strategy/strategy_factory.hpp
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <mutex>
#include "winter/strategy/strategy_base.hpp"

namespace winter {
namespace strategy {

class StrategyFactory {
private:
    using StrategyCreator = std::function<StrategyPtr()>;
    static std::unordered_map<std::string, StrategyCreator> creators_;
    static std::mutex factory_mutex_;

public:
    // Register a strategy type with the factory
    template<typename T>
    static void register_type(const std::string& type_name) {
        std::lock_guard<std::mutex> lock(factory_mutex_);
        // Capture type_name by value in the lambda
        creators_[type_name] = [type_name]() -> StrategyPtr { 
            return std::make_shared<T>(type_name); 
        };
    }
    
    // Create a strategy instance by type name
    static StrategyPtr create_strategy(const std::string& type_name) {
        std::lock_guard<std::mutex> lock(factory_mutex_);
        auto it = creators_.find(type_name);
        if (it != creators_.end()) {
            return it->second();
        }
        return nullptr;
    }
    
    // Get all registered strategy types
    static std::vector<std::string> get_registered_types() {
        std::lock_guard<std::mutex> lock(factory_mutex_);
        std::vector<std::string> types;
        for (const auto& [type, _] : creators_) {
            types.push_back(type);
        }
        return types;
    }
};

} // namespace strategy
} // namespace winter
