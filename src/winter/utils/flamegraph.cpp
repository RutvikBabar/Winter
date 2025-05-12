#include <winter/utils/flamegraph.hpp>
#include <winter/utils/logger.hpp>
#include <iostream>
#include <fstream>
#include <cstdlib>
#include <thread>
#include <sstream>

#ifdef __linux__
#include <sys/types.h>
#include <unistd.h>
#endif

namespace winter::utils {

class FlamegraphImpl {
private:
    std::string name_;
    pid_t pid_;
    std::string perf_data_file_;
    
public:
    explicit FlamegraphImpl(std::string name) 
        : name_(std::move(name)), pid_(getpid()) {
        perf_data_file_ = name_ + ".perf.data";
    }
    
    void start_profiling() {
        #ifdef __linux__
        std::stringstream cmd;
        cmd << "perf record -F 99 -p " << pid_ 
            << " -g -o " << perf_data_file_ << " &";
        std::system(cmd.str().c_str());
        Logger::info() << "Started profiling with perf" << Logger::endl;
        #else
        Logger::warn() << "Flamegraph profiling is only supported on Linux" << Logger::endl;
        #endif
    }
    
    void stop_profiling() {
        #ifdef __linux__
        std::system("pkill -SIGINT perf");
        std::this_thread::sleep_for(std::chrono::seconds(1));
        Logger::info() << "Stopped profiling with perf" << Logger::endl;
        #endif
    }
    
    void generate_flamegraph() {
        #ifdef __linux__
        Logger::info() << "Generating flamegraph..." << Logger::endl;
        
        std::stringstream cmd;
        cmd << "perf script -i " << perf_data_file_ 
            << " | stackcollapse-perf.pl > " << name_ << ".folded";
        int ret1 = std::system(cmd.str().c_str());
        
        if (ret1 != 0) {
            Logger::error() << "Failed to collapse stack frames" << Logger::endl;
            return;
        }
        
        cmd.str("");
        cmd << "flamegraph.pl " << name_ << ".folded > " << name_ << ".svg";
        int ret2 = std::system(cmd.str().c_str());
        
        if (ret2 != 0) {
            Logger::error() << "Failed to generate flamegraph" << Logger::endl;
            return;
        }
        
        Logger::info() << "Flamegraph generated: " << name_ << ".svg" << Logger::endl;
        #else
        Logger::warn() << "Flamegraph generation is only supported on Linux" << Logger::endl;
        #endif
    }
};

Flamegraph::Flamegraph(std::string name) 
    : name_(std::move(name)), impl_(std::make_unique<FlamegraphImpl>(name_)), running_(false) {}

Flamegraph::~Flamegraph() {
    if (running_) {
        stop();
    }
}

void Flamegraph::start() {
    if (!running_) {
        impl_->start_profiling();
        start_time_ = std::chrono::high_resolution_clock::now();
        running_ = true;
    }
}

void Flamegraph::stop() {
    if (running_) {
        impl_->stop_profiling();
        running_ = false;
    }
}

void Flamegraph::generate_report() {
    impl_->generate_flamegraph();
}

} // namespace winter::utils
