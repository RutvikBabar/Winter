#pragma once
#include <atomic>
#include <array>
#include <optional>

namespace winter::utils {

template <typename T, size_t Capacity>
class LockFreeQueue {
private:
    std::array<T, Capacity> buffer;
    std::array<std::atomic<bool>, Capacity> occupied{};
    
    std::atomic<size_t> head{0};
    std::atomic<size_t> tail{0};
    
public:
    LockFreeQueue() {
        for (auto& flag : occupied) {
            flag.store(false, std::memory_order_relaxed);
        }
    }
    
    bool push(const T& item) {
        size_t current_tail = tail.load(std::memory_order_relaxed);
        size_t next_tail = (current_tail + 1) % Capacity;
        
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false; // Queue is full
        }
        
        buffer[current_tail] = item;
        occupied[current_tail].store(true, std::memory_order_release);
        tail.store(next_tail, std::memory_order_release);
        return true;
    }
    
    std::optional<T> pop() {
        size_t current_head = head.load(std::memory_order_relaxed);
        
        if (current_head == tail.load(std::memory_order_acquire)) {
            return std::nullopt; // Queue is empty
        }
        
        if (!occupied[current_head].load(std::memory_order_acquire)) {
            return std::nullopt; // Data not yet fully written
        }
        
        T result = buffer[current_head];
        occupied[current_head].store(false, std::memory_order_release);
        head.store((current_head + 1) % Capacity, std::memory_order_release);
        return result;
    }
    
    bool empty() const {
        return head.load(std::memory_order_acquire) == 
               tail.load(std::memory_order_acquire);
    }
};

} // namespace winter::utils
