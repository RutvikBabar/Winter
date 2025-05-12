#include <gtest/gtest.h>
#include <winter/utils/memory_pool.hpp>
#include <winter/utils/lock_free_queue.hpp>
#include <winter/utils/core_affinity.hpp>
#include <winter/utils/logger.hpp>

#include <thread>
#include <vector>
#include <atomic>
#include <string>

// Test memory pool
TEST(MemoryPoolTest, BasicOperations) {
    winter::utils::MemoryPool<int> pool;
    
    // Allocate memory
    int* ptr1 = pool.allocate();
    int* ptr2 = pool.allocate();
    
    // Check that pointers are different
    EXPECT_NE(ptr1, ptr2);
    
    // Set and check values
    *ptr1 = 42;
    *ptr2 = 84;
    EXPECT_EQ(*ptr1, 42);
    EXPECT_EQ(*ptr2, 84);
    
    // Deallocate memory
    pool.deallocate(ptr1);
    pool.deallocate(ptr2);
    
    // Allocate again and check if we get the same memory
    int* ptr3 = pool.allocate();
    EXPECT_TRUE(ptr3 == ptr1 || ptr3 == ptr2);
}

// Test lock-free queue
TEST(LockFreeQueueTest, BasicOperations) {
    winter::utils::LockFreeQueue<int, 10> queue;
    
    // Check empty queue
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.pop().has_value());
    
    // Push items
    EXPECT_TRUE(queue.push(1));
    EXPECT_TRUE(queue.push(2));
    EXPECT_TRUE(queue.push(3));
    
    // Queue should not be empty
    EXPECT_FALSE(queue.empty());
    
    // Pop items
    auto item1 = queue.pop();
    ASSERT_TRUE(item1.has_value());
    EXPECT_EQ(*item1, 1);
    
    auto item2 = queue.pop();
    ASSERT_TRUE(item2.has_value());
    EXPECT_EQ(*item2, 2);
    
    auto item3 = queue.pop();
    ASSERT_TRUE(item3.has_value());
    EXPECT_EQ(*item3, 3);
    
    // Queue should be empty again
    EXPECT_TRUE(queue.empty());
    EXPECT_FALSE(queue.pop().has_value());
}

// Test lock-free queue with multiple threads
TEST(LockFreeQueueTest, MultiThreaded) {
    winter::utils::LockFreeQueue<int, 1000> queue;
    std::atomic<int> sum(0);
    
    // Producer thread
    std::thread producer([&queue]() {
        for (int i = 1; i <= 100; ++i) {
            while (!queue.push(i)) {
                std::this_thread::yield();
            }
        }
    });
    
    // Consumer threads
    std::vector<std::thread> consumers;
    for (int i = 0; i < 4; ++i) {
        consumers.emplace_back([&queue, &sum]() {
            while (sum.load() < 5050) { // Sum of 1 to 100
                auto item = queue.pop();
                if (item) {
                    sum.fetch_add(*item, std::memory_order_relaxed);
                } else {
                    std::this_thread::yield();
                }
            }
        });
    }
    
    // Join threads
    producer.join();
    for (auto& consumer : consumers) {
        consumer.join();
    }
    
    // Check final sum
    EXPECT_EQ(sum.load(), 5050);
}

// Test logger
TEST(LoggerTest, BasicLogging) {
    // Redirect cout to capture log output
    std::stringstream buffer;
    std::streambuf* old = std::cout.rdbuf(buffer.rdbuf());
    
    // Set log level to INFO
    winter::utils::Logger::set_level(winter::utils::LogLevel::INFO);
    
    // Log messages at different levels
    winter::utils::Logger::debug() << "Debug message" << winter::utils::Logger::endl;
    winter::utils::Logger::info() << "Info message" << winter::utils::Logger::endl;
    winter::utils::Logger::warn() << "Warning message" << winter::utils::Logger::endl;
    winter::utils::Logger::error() << "Error message" << winter::utils::Logger::endl;
    
    // Restore cout
    std::cout.rdbuf(old);
    
    // Check log output
    std::string output = buffer.str();
    EXPECT_EQ(output.find("Debug message"), std::string::npos); // Debug should not be logged
    EXPECT_NE(output.find("Info message"), std::string::npos);
    EXPECT_NE(output.find("Warning message"), std::string::npos);
    EXPECT_NE(output.find("Error message"), std::string::npos);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
