// include/winter/utils/config.hpp
#pragma once
#include <string>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <fstream>
#include <sstream>

namespace winter {
namespace utils {

class Config {
private:
    std::unordered_map<std::string, std::string> values_;
    mutable std::mutex mutex_;
    static std::shared_ptr<Config> instance_;
    static std::mutex instance_mutex_;

public:
    Config() = default;
    
    // Singleton access
    static std::shared_ptr<Config> instance() {
        std::lock_guard<std::mutex> lock(instance_mutex_);
        if (!instance_) {
            instance_ = std::make_shared<Config>();
        }
        return instance_;
    }
    
    // Load configuration from file
    bool load_from_file(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        values_.clear();
        std::string line;
        while (std::getline(file, line)) {
            // Skip comments and empty lines
            if (line.empty() || line[0] == '#') {
                continue;
            }
            
            // Parse key=value
            size_t pos = line.find('=');
            if (pos != std::string::npos) {
                std::string key = line.substr(0, pos);
                std::string value = line.substr(pos + 1);
                
                // Trim whitespace
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                value.erase(0, value.find_first_not_of(" \t"));
                value.erase(value.find_last_not_of(" \t") + 1);
                
                values_[key] = value;
            }
        }
        
        return true;
    }
    
    // Get a value with default
    template<typename T>
    T get(const std::string& key, const T& default_value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        if (it == values_.end()) {
            return default_value;
        }
        
        std::istringstream iss(it->second);
        T value;
        if (!(iss >> value)) {
            return default_value;
        }
        
        return value;
    }
    
    // Specialized for string to avoid stringstream
    std::string get(const std::string& key, const std::string& default_value) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = values_.find(key);
        return (it != values_.end()) ? it->second : default_value;
    }
    
    // Set a value
    template<typename T>
    void set(const std::string& key, const T& value) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::ostringstream oss;
        oss << value;
        values_[key] = oss.str();
    }
};

} // namespace utils
} // namespace winter
