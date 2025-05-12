#pragma once
#include <vector>
#include <cstddef>
#include <atomic>
#include <array>
#include <cassert>
#include <memory>

namespace winter::utils {

template <typename T, size_t BlockSize = 4096>
class MemoryPool {
private:
    // Modified Block struct to make it copyable/movable
    struct Block {
        std::array<T, BlockSize> data;
        std::array<bool, BlockSize> used{false};
        // Use a raw pointer instead of atomic to avoid copy/move issues
        size_t next_free_index{0};
        
        Block() : next_free_index(0) {
            for (auto& flag : used) {
                flag = false;
            }
        }
    };
    
    std::vector<std::unique_ptr<Block>> blocks;
    std::atomic<size_t> current_block{0};
    
    void update_next_free_index(Block& block) {
        size_t idx = block.next_free_index;
        while (idx < BlockSize) {
            if (!block.used[idx]) {
                block.next_free_index = idx;
                return;
            }
            idx++;
        }
        block.next_free_index = BlockSize;
    }
    
public:
    MemoryPool(size_t initial_blocks = 1) {
        for (size_t i = 0; i < initial_blocks; ++i) {
            blocks.push_back(std::make_unique<Block>());
        }
    }
    
    T* allocate() {
        for (size_t b = 0; b < blocks.size(); ++b) {
            Block& block = *blocks[b];
            size_t idx = block.next_free_index;
            
            if (idx < BlockSize) {
                if (!block.used[idx]) {
                    block.used[idx] = true;
                    update_next_free_index(block);
                    return &block.data[idx];
                }
            }
        }
        
        // Need a new block
        size_t new_block_idx = blocks.size();
        blocks.push_back(std::make_unique<Block>());
        blocks[new_block_idx]->used[0] = true;
        blocks[new_block_idx]->next_free_index = 1;
        return &blocks[new_block_idx]->data[0];
    }
    
    void deallocate(T* ptr) {
        for (size_t b = 0; b < blocks.size(); ++b) {
            Block& block = *blocks[b];
            T* block_start = &block.data[0];
            T* block_end = block_start + BlockSize;
            
            if (ptr >= block_start && ptr < block_end) {
                size_t idx = ptr - block_start;
                assert(block.used[idx]);
                block.used[idx] = false;
                
                size_t current_next_free = block.next_free_index;
                if (idx < current_next_free) {
                    block.next_free_index = idx;
                }
                return;
            }
        }
        assert(false && "Pointer not found in memory pool");
    }
};

} // namespace winter::utils
