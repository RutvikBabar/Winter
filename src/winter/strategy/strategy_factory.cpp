// src/winter/strategy/strategy_factory.cpp
#include "winter/strategy/strategy_factory.hpp"

namespace winter {
namespace strategy {

// Define static members
std::unordered_map<std::string, StrategyFactory::StrategyCreator> StrategyFactory::creators_;
std::mutex StrategyFactory::factory_mutex_;

} // namespace strategy
} // namespace winter
