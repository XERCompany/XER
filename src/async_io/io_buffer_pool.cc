#include "io_buffer_pool.h"

#include <algorithm>
#include <cassert>

namespace AsyncIO::IO::Async {

IOBufferPool &IOBufferPool::Instance() {
    static IOBufferPool instance;
    return instance;
}

IOBufferPool::IOBufferPool(size_t minBucketSize, size_t maxBucketSize, size_t maxPerBucket)
    : minBucketSize_(std::max<size_t>(1, minBucketSize)),
      maxBucketSize_(std::max(minBucketSize_, maxBucketSize)),
      maxPerBucket_(maxPerBucket) {
    size_t bucketSize = minBucketSize_;
    while (bucketSize <= maxBucketSize_) {
        buckets_.emplace_back();
        buckets_.back().size = bucketSize;
        if (bucketSize > (SIZE_MAX / 2)) {
            break;
        }
        bucketSize <<= 1;
    }
}

size_t IOBufferPool::GetMinBucketSize() const noexcept {
    return minBucketSize_;
}

size_t IOBufferPool::GetMaxBucketSize() const noexcept {
    return maxBucketSize_;
}

size_t IOBufferPool::RoundUpToBucket(size_t size) const noexcept {
    if (size <= minBucketSize_) {
        return minBucketSize_;
    }
    size_t result = minBucketSize_;
    while (result < size && result < maxBucketSize_) {
        result <<= 1;
    }
    if (result < size) {
        result = size;
    }
    return result;
}

size_t IOBufferPool::BucketIndexForSize(size_t size) const noexcept {
    size = RoundUpToBucket(size);
    size_t index = 0;
    size_t bucketSize = minBucketSize_;
    while (index + 1 < buckets_.size() && bucketSize < size) {
        bucketSize <<= 1;
        ++index;
    }

    if (bucketSize < size) {
        index = buckets_.size() - 1;
    }

    return index;
}

IOBufferPool::BufferPtr IOBufferPool::Acquire(size_t minSize) {
    const size_t targetSize = RoundUpToBucket(minSize);
    const size_t idx = BucketIndexForSize(targetSize);

    Bucket &bucket = buckets_[idx];
    {
        std::lock_guard<std::mutex> guard(bucket.lock);
        if (!bucket.freeList.empty()) {
            Buffer *reusable = bucket.freeList.back();
            bucket.freeList.pop_back();
            reusable->clear();
            reusable->reserve(bucket.size);
            return BufferPtr(reusable, [this](Buffer *p) noexcept { Release(p); });
        }
    }

    Buffer *newBuffer = new Buffer();
    newBuffer->reserve(targetSize);
    return BufferPtr(newBuffer, [this](Buffer *p) noexcept { Release(p); });
}

void IOBufferPool::Release(Buffer *buffer) noexcept {
    if (buffer == nullptr) {
        return;
    }

    size_t capacity = buffer->capacity();
    size_t bucketSize = RoundUpToBucket(capacity);

    if (bucketSize > maxBucketSize_ || bucketSize < minBucketSize_) {
        delete buffer;
        return;
    }

    size_t idx = BucketIndexForSize(bucketSize);
    Bucket &bucket = buckets_[idx];

    std::lock_guard<std::mutex> guard(bucket.lock);
    if (bucket.freeList.size() >= maxPerBucket_) {
        delete buffer;
    } else {
        buffer->clear();
        bucket.freeList.push_back(buffer);
    }
}

void IOBufferPool::Clear() noexcept {
    for (Bucket &bucket : buckets_) {
        std::lock_guard<std::mutex> guard(bucket.lock);
        for (Buffer *p : bucket.freeList) {
            delete p;
        }
        bucket.freeList.clear();
    }
}

} // namespace AsyncIO::IO::Async
