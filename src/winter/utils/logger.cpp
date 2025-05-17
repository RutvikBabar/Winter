#include <winter/utils/logger.hpp>
#include <iostream>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace winter::utils {

// Static member initialization
std::mutex Logger::console_mutex_;
LogLevel Logger::current_level_ = LogLevel::INFO;

Logger::Logger(LogLevel level) : level_(level) {}

Logger& Logger::debug() {
    static thread_local Logger instance(LogLevel::DEBUG);
    instance.stream_.str("");
    return instance;
}

Logger& Logger::info() {
    static thread_local Logger instance(LogLevel::INFO);
    instance.stream_.str("");
    return instance;
}

Logger& Logger::warn() {
    static thread_local Logger instance(LogLevel::WARN);
    instance.stream_.str("");
    return instance;
}

Logger& Logger::error() {
    static thread_local Logger instance(LogLevel::LOG_ERROR);
    instance.stream_.str("");
    return instance;
}

Logger& Logger::operator<<(const EndlType&) {
    if (level_ >= current_level_) {
        // Get current time
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count() % 1000;
        
        std::stringstream time_str;
        time_str << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
        time_str << '.' << std::setfill('0') << std::setw(3) << ms;
        
        // Lock console for thread-safe output
        std::lock_guard<std::mutex> lock(console_mutex_);
        
        // Output log message with timestamp and level
        std::cout << "[" << time_str.str() << "] ";
        
        switch (level_) {
            case LogLevel::DEBUG:
                std::cout << "[DEBUG] ";
                break;
            case LogLevel::INFO:
                std::cout << "[INFO] ";
                break;
            case LogLevel::WARN:
                std::cout << "[WARN] ";
                break;
            case LogLevel::LOG_ERROR:
                std::cout << "[ERROR] ";
                break;
        }
        
        std::cout << stream_.str() << std::endl;
    }
    
    return *this;
}

void Logger::set_level(LogLevel level) {
    current_level_ = level;
}


} // namespace winter::utils
