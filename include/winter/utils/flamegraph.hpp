#pragma once
#include <string>
#include <memory>
#include <chrono>

namespace winter::utils {

class FlamegraphImpl;

class Flamegraph {
private:
    std::unique_ptr<FlamegraphImpl> impl_;
    std::string name_;
    bool running_ = false;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_time_;
    
public:
    explicit Flamegraph(std::string name);
    ~Flamegraph();
    
    void start();
    void stop();
    void generate_report();
};

} // namespace winter::utils
