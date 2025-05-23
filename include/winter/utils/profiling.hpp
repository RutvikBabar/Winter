// include/winter/utils/profiling.hpp
#pragma once
#include <string>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include "winter/utils/logger.hpp"

namespace winter {
namespace utils {

class FlameGraphProfiler {
private:
    bool is_profiling_ = false;
    std::string session_name_;
    std::string output_dir_ = "profiles";
    std::chrono::system_clock::time_point start_time_;
    
    // Generate a timestamp-based filename
    std::string generate_filename() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << output_dir_ << "/winter_profile_";
        ss << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S");
        ss << ".etl";
        return ss.str();
    }
    
    // Convert ETL to flame graph using WPA
    void generate_flame_graph(const std::string& etl_file) {
        // Create output directory if it doesn't exist
        std::filesystem::create_directories(output_dir_);
        
        // Base filename without extension
        std::string base_file = etl_file.substr(0, etl_file.find_last_of('.'));
        std::string svg_file = base_file + ".svg";
        
        // Command to process ETL file with Windows Performance Recorder
        // This uses xperf to convert ETL to a format that can be visualized
        std::string command = "xperf -i \"" + etl_file + "\" -o \"" + base_file + ".summary.txt\" -a cpustack";
        
        // Execute the command
        int result = system(command.c_str());
        if (result != 0) {
            winter::utils::Logger::error() << "Failed to process ETL file: " << etl_file 
                                      << winter::utils::Logger::endl;
            return;
        }
        
        // Use flamegraph.pl to convert the stack samples to SVG
        // Note: This assumes you have flamegraph.pl installed
        command = "flamegraph.pl \"" + base_file + ".summary.txt\" > \"" + svg_file + "\"";
        result = system(command.c_str());
        if (result != 0) {
            winter::utils::Logger::error() << "Failed to generate flame graph SVG" 
                                      << winter::utils::Logger::endl;
            return;
        }
        
        winter::utils::Logger::info() << "Flame graph generated: " << svg_file 
                                  << winter::utils::Logger::endl;
    }

public:
    FlameGraphProfiler(const std::string& name = "WinterProfile") : session_name_(name) {}
    
    void set_output_directory(const std::string& dir) {
        output_dir_ = dir;
    }
    
    bool start() {
        if (is_profiling_) return false;
        
        start_time_ = std::chrono::system_clock::now();
        
        // Create output directory if it doesn't exist
        std::filesystem::create_directories(output_dir_);
        
        // Start ETW CPU sampling session
        // This is a simplified example - actual implementation requires more ETW setup
        std::string command = "wpr -start CPU";
        int result = system(command.c_str());
        
        if (result != 0) {
            winter::utils::Logger::error() << "Failed to start profiling" 
                                      << winter::utils::Logger::endl;
            return false;
        }
        
        is_profiling_ = true;
        winter::utils::Logger::info() << "Profiling started" << winter::utils::Logger::endl;
        return true;
    }
    
    bool stop() {
        if (!is_profiling_) return false;
        
        // Generate filename based on timestamp
        std::string output_file = generate_filename();
        
        // Stop ETW session and save to file
        std::string command = "wpr -stop \"" + output_file + "\"";
        int result = system(command.c_str());
        
        if (result != 0) {
            winter::utils::Logger::error() << "Failed to stop profiling" 
                                      << winter::utils::Logger::endl;
            return false;
        }
        
        // Calculate duration
        auto end_time = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time_).count();
        
        winter::utils::Logger::info() << "Profiling stopped after " << duration 
                                  << " ms, data saved to " << output_file 
                                  << winter::utils::Logger::endl;
        
        // Generate flame graph from ETL file
        generate_flame_graph(output_file);
        
        is_profiling_ = false;
        return true;
    }
};

} // namespace utils
} // namespace winter
