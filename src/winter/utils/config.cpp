// src/winter/utils/config.cpp
#include "winter/utils/config.hpp"

namespace winter {
namespace utils {

// Define static members
std::shared_ptr<Config> Config::instance_ = nullptr;
std::mutex Config::instance_mutex_;

} // namespace utils
} // namespace winter
