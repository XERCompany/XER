#ifndef IO_ASYNC_IO_BUFFER_POOL_H
#define IO_ASYNC_IO_BUFFER_POOL_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <vector>

namespace AsyncIO::IO::Async {

class IOBufferPool {
public:
    using Buffer = std::vector<std::uint8_t>;
    using BufferPtr = std::shared_ptr<Buffer>;

    static IOBufferPool &Instance();

    explicit IOBufferPool(size_t minBucketSize = 64, size_t maxBucketSize = 1 << 20, size_t maxPerBucket = 128);
    IOBufferPool(const IOBufferPool &) = delete;
    IOBufferPool &operator=(const IOBufferPool &) = delete;

    BufferPtr Acquire(size_t minSize);
    void Release(Buffer *buffer) noexcept;

    size_t GetMinBucketSize() const noexcept;
    size_t GetMaxBucketSize() const noexcept;

    void Clear() noexcept;

private:
    size_t RoundUpToBucket(size_t size) const noexcept;
    size_t BucketIndexForSize(size_t size) const noexcept;

    const size_t minBucketSize_; 
    const size_t maxBucketSize_; 
    const size_t maxPerBucket_;

    struct Bucket {
        size_t size;
        mutable std::mutex lock;
        std::vector<Buffer *> freeList;
    };

    std::deque<Bucket> buckets_;
};

} // namespace AsyncIO::IO::Async

#endif // IO_ASYNC_IO_BUFFER_POOL_H
