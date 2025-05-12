#pragma once
#include <string>
#include <sstream>
#include <mutex>

namespace winter::utils {

enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    LOG_ERROR  // Changed from ERROR to avoid Windows macro conflict
};


class Logger {
public:
    struct EndlType {};
    static const EndlType endl;

    static Logger& debug();
    static Logger& info();
    static Logger& warn();
    static Logger& error();

    template<typename T>
    Logger& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }

    Logger& operator<<(const EndlType&);

    static void set_level(LogLevel level);

private:
    explicit Logger(LogLevel level);
    
    LogLevel level_;
    std::stringstream stream_;
    
    static std::mutex console_mutex_;
    static LogLevel current_level_;
};

} // namespace winter::utils
