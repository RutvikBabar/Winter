#pragma once
#include <thread>
#include <functional>
#include <memory>
#include <atomic>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace winter::utils {

class CoreAffinity {
public:
    static bool pin_thread_to_core(std::thread::native_handle_type handle, int core_id) {
#ifdef _WIN32
        // Convert native_handle_type to HANDLE for Windows
        DWORD threadId = static_cast<DWORD>(handle);
        HANDLE threadHandle = OpenThread(THREAD_ALL_ACCESS, FALSE, threadId);
        if (!threadHandle) {
            return false;
        }
        DWORD_PTR mask = 1ULL << core_id;
        bool result = SetThreadAffinityMask(threadHandle, mask) != 0;
        CloseHandle(threadHandle);
        return result;
#else
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(core_id, &cpuset);
        return pthread_setaffinity_np(handle, sizeof(cpu_set_t), &cpuset) == 0;
#endif
    }
    
    template<typename T, typename... A>
    static std::unique_ptr<std::thread> create_pinned_thread(int core_id, T&& fn, A&&... args) {
        std::atomic<bool> running{false};
        std::atomic<bool> failed{false};
        
        auto thread_body = [core_id, &running, &failed, fn = std::forward<T>(fn), 
                           ... args = std::forward<A>(args)]() mutable {
#ifdef _WIN32
            // On Windows, we need to pin after thread creation
            HANDLE currentThread = GetCurrentThread();
            DWORD_PTR mask = 1ULL << core_id;
            bool success = SetThreadAffinityMask(currentThread, mask) != 0;
#else
            // On Linux, use pthread API
            bool success = CoreAffinity::pin_thread_to_core(pthread_self(), core_id);
#endif
            if (!success) {
                failed = true;
                return;
            }
            
            running = true;
            std::forward<T>(fn)(std::forward<A>(args)...);
        };
        
        auto t = std::make_unique<std::thread>(thread_body);
        
        while (!(running || failed)) {
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(50ms);
        }
        
        if (failed) {
            t->join();
            throw std::runtime_error("Failed to pin thread to core " + std::to_string(core_id));
        }
        
        return t;
    }
};

} // namespace winter::utils
