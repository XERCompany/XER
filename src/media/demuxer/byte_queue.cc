#include "media/demuxer/byte_queue.h"

#include <algorithm>
#include <cstring>

namespace XER::Media {

ByteQueue::ByteQueue(std::size_t initial_capacity)
    : buf_(initial_capacity) {}

void ByteQueue::Append(const std::uint8_t* data, std::size_t size) {
    if (size_ + size > buf_.size()) {
        std::vector<std::uint8_t> next((size_ + size) * 2);
        Peek(next.data(), size_);
        buf_.swap(next);
        head_ = 0;
    }
    const std::size_t tail = (head_ + size_) % buf_.size();
    const std::size_t first = std::min(size, buf_.size() - tail);
    std::memcpy(buf_.data() + tail, data, first);
    if (size > first)
        std::memcpy(buf_.data(), data + first, size - first);
    size_ += size;
}

std::size_t ByteQueue::Peek(std::uint8_t* out, std::size_t size) const {
    const std::size_t n = std::min(size, size_);
    const std::size_t first = std::min(n, buf_.size() - head_);
    std::memcpy(out, buf_.data() + head_, first);
    if (n > first) std::memcpy(out + first, buf_.data(), n - first);
    return n;
}

void ByteQueue::Consume(std::size_t size) {
    const std::size_t n = std::min(size, size_);
    head_ = (head_ + n) % buf_.size();
    size_ -= n;
    head_offset_ += n;
    if (size_ == 0) head_ = 0;
}

void ByteQueue::Clear() {
    head_ = size_ = 0;
}

}  // namespace XER::Media
