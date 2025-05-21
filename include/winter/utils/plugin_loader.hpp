// include/winter/utils/plugin_loader.hpp
#pragma once
#include <string>
#include <memory>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace winter {
namespace utils {

class PluginLoader {
private:
#ifdef _WIN32
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif

public:
    PluginLoader() = default;
    ~PluginLoader() {
        unload();
    }
    
    // Load a plugin from a shared library
    bool load(const std::string& path) {
        unload(); // Unload any previously loaded library
        
#ifdef _WIN32
        handle_ = LoadLibraryA(path.c_str());
        if (!handle_) {
            throw std::runtime_error("Failed to load plugin: " + path);
        }
#else
        handle_ = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle_) {
            throw std::runtime_error("Failed to load plugin: " + std::string(dlerror()));
        }
#endif
        return true;
    }
    
    // Unload the plugin
    void unload() {
        if (handle_) {
#ifdef _WIN32
            FreeLibrary(handle_);
#else
            dlclose(handle_);
#endif
            handle_ = nullptr;
        }
    }
    
    // Get a function from the plugin
    template<typename T>
    T get_function(const std::string& name) {
        if (!handle_) {
            throw std::runtime_error("No plugin loaded");
        }
        
#ifdef _WIN32
        T func = reinterpret_cast<T>(GetProcAddress(handle_, name.c_str()));
        if (!func) {
            throw std::runtime_error("Failed to get function: " + name);
        }
#else
        T func = reinterpret_cast<T>(dlsym(handle_, name.c_str()));
        if (!func) {
            throw std::runtime_error("Failed to get function: " + std::string(dlerror()));
        }
#endif
        return func;
    }
};

} // namespace utils
} // namespace winter
