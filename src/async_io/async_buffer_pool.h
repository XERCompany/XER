#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <cstdint>

namespace AsyncIO::IO::Async {

struct BufferBlock {
    std::vector<uint8_t> data;
    size_t used_bytes;
    bool in_use;

    BufferBlock(size_t capacity)
        : data(capacity), used_bytes(0), in_use(false) {}
};

class AsyncBufferPool {
public:
    explicit AsyncBufferPool(size_t block_size, size_t initial_blocks = 4);
    ~AsyncBufferPool();

    // Buffer acquisition
    std::shared_ptr<BufferBlock> AcquireBuffer();
    void ReleaseBuffer(std::shared_ptr<BufferBlock> buffer);

    // Pool statistics
    size_t GetBlockSize() const { return block_size_; }
    size_t GetAvailableBlocks() const;
    size_t GetTotalBlocks() const;
    size_t GetUsedBlocks() const;

    // Pool management
    void Resize(size_t total_blocks);
    void Reset();
    void Trim();

private:
    size_t block_size_;
    std::vector<std::shared_ptr<BufferBlock>> all_blocks_;
    std::queue<std::shared_ptr<BufferBlock>> available_blocks_;
    mutable std::mutex pool_mutex_;

    void AllocateBlocks(size_t count);
};

}  // namespace AsyncIO::IO::Async
