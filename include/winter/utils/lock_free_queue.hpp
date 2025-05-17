#pragma once

#include <atomic>
#include <array>

namespace winter::utils {

template<typename T, size_t Capacity = 1024>
class LockFreeQueue {
private:
    std::array<T, Capacity> buffer;
    std::array<std::atomic<bool>, Capacity> occupied;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    
public:
    LockFreeQueue() {
        head.store(0, std::memory_order_relaxed);
        tail.store(0, std::memory_order_relaxed);
        
        for (auto& flag : occupied) {
            flag.store(false, std::memory_order_relaxed);
        }
    }
    
    bool push(const T& item) {
        size_t current_tail = tail.load(std::memory_order_acquire);
        size_t next_tail = (current_tail + 1) % Capacity;
        
        // Check if queue is full
        if (next_tail == head.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Store item in buffer
        buffer[current_tail] = item;
        occupied[current_tail].store(true, std::memory_order_release);
        
        // Update tail
        tail.store(next_tail, std::memory_order_release);
        
        return true;
    }
    
    bool pop(T& item) {
        size_t current_head = head.load(std::memory_order_acquire);
        
        // Check if queue is empty
        if (current_head == tail.load(std::memory_order_acquire)) {
            return false;
        }
        
        // Check if item is ready
        if (!occupied[current_head].load(std::memory_order_acquire)) {
            return false;
        }
        
        // Get item from buffer
        item = buffer[current_head];
        occupied[current_head].store(false, std::memory_order_release);
        
        // Update head
        head.store((current_head + 1) % Capacity, std::memory_order_release);
        
        return true;
    }
    
    bool empty() const {
        return head.load(std::memory_order_acquire) ==
               tail.load(std::memory_order_acquire);
    }
    
    size_t size() const {
        size_t h = head.load(std::memory_order_acquire);
        size_t t = tail.load(std::memory_order_acquire);
        
        if (t >= h) {
            return t - h;
        } else {
            return Capacity - h + t;
        }
    }
    
    size_t capacity() const {
        return Capacity;
    }
};

} // namespace winter::utils
