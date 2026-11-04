#include "async_buffer_pool.h"

#include <algorithm>

namespace AsyncIO::IO::Async {

AsyncBufferPool::AsyncBufferPool(size_t block_size, size_t initial_blocks)
    : block_size_(block_size) {
    AllocateBlocks(initial_blocks);
}

AsyncBufferPool::~AsyncBufferPool() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    all_blocks_.clear();
    while (!available_blocks_.empty()) {
        available_blocks_.pop();
    }
}

std::shared_ptr<BufferBlock> AsyncBufferPool::AcquireBuffer() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    if (available_blocks_.empty()) {
        AllocateBlocks(1);
    }

    auto buffer = available_blocks_.front();
    available_blocks_.pop();
    buffer->in_use = true;
    buffer->used_bytes = 0;
    
    return buffer;
}

void AsyncBufferPool::ReleaseBuffer(std::shared_ptr<BufferBlock> buffer) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    if (!buffer || !buffer->in_use) {
        return;
    }

    if (std::find(all_blocks_.begin(), all_blocks_.end(), buffer) == all_blocks_.end()) {
        return;
    }

    buffer->in_use = false;
    buffer->used_bytes = 0;
    available_blocks_.push(buffer);
}

size_t AsyncBufferPool::GetAvailableBlocks() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return available_blocks_.size();
}

size_t AsyncBufferPool::GetTotalBlocks() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return all_blocks_.size();
}

size_t AsyncBufferPool::GetUsedBlocks() const {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    return available_blocks_.size() <= all_blocks_.size()
        ? all_blocks_.size() - available_blocks_.size()
        : 0;
}

void AsyncBufferPool::Resize(size_t total_blocks) {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    
    size_t current_size = all_blocks_.size();
    if (total_blocks > current_size) {
        AllocateBlocks(total_blocks - current_size);
    } else if (total_blocks < current_size) {
        all_blocks_.resize(total_blocks);
        while (!available_blocks_.empty()) {
            available_blocks_.pop();
        }
        for (auto& block : all_blocks_) {
            if (!block->in_use) {
                available_blocks_.push(block);
            }
        }
    }
}

void AsyncBufferPool::Reset() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    available_blocks_ = std::queue<std::shared_ptr<BufferBlock>>();
    for (auto& block : all_blocks_) {
        block->in_use = false;
        block->used_bytes = 0;
        available_blocks_.push(block);
    }
}

void AsyncBufferPool::Trim() {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    all_blocks_.clear();
    while (!available_blocks_.empty()) {
        available_blocks_.pop();
    }
}

void AsyncBufferPool::AllocateBlocks(size_t count) {
    all_blocks_.reserve(all_blocks_.size() + count);
    for (size_t i = 0; i < count; ++i) {
        auto block = std::make_shared<BufferBlock>(block_size_);
        all_blocks_.push_back(block);
        available_blocks_.push(block);
    }
}

}  // namespace AsyncIO::IO::Async
