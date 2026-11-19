#include "media/base/seekable_buffer.h"

#include <algorithm>
#include <cstring>

namespace XER::Media {

void SeekableBuffer::Append(const std::uint8_t* data, std::size_t size,
                            TimeTicks timestamp) {
    if (!data || !size) return;
    Block b;
    b.data.assign(data, data + size);
    b.timestamp = timestamp;
    size_ += size;
    blocks_.push_back(std::move(b));
}

std::size_t SeekableBuffer::Peek(std::uint8_t* out, std::size_t size) const {
    std::size_t done = 0;
    for (const auto& b : blocks_) {
        if (done >= size) break;
        const std::size_t avail = b.data.size() - b.consumed;
        const std::size_t n = std::min(avail, size - done);
        std::memcpy(out + done, b.data.data() + b.consumed, n);
        done += n;
    }
    return done;
}

std::size_t SeekableBuffer::Read(std::uint8_t* out, std::size_t size,
                                 TimeTicks* timestamp_out) {
    if (timestamp_out)
        *timestamp_out =
            blocks_.empty() ? kNoTimestamp : blocks_.front().timestamp;
    std::size_t done = 0;
    while (done < size && !blocks_.empty()) {
        Block& b = blocks_.front();
        const std::size_t avail = b.data.size() - b.consumed;
        const std::size_t n = std::min(avail, size - done);
        std::memcpy(out + done, b.data.data() + b.consumed, n);
        b.consumed += n;
        done += n;
        size_ -= n;
        if (b.consumed == b.data.size()) blocks_.pop_front();
    }
    return done;
}

void SeekableBuffer::Skip(std::size_t size) {
    std::uint8_t scratch[512];
    while (size) {
        const std::size_t n = std::min(size, sizeof scratch);
        const std::size_t got = Read(scratch, n, nullptr);
        if (!got) break;
        size -= got;
    }
}

bool SeekableBuffer::SeekBackward(TimeTicks target) {
    if (blocks_.empty()) return false;
    if (blocks_.front().timestamp == kNoTimestamp ||
        target < blocks_.front().timestamp)
        return false;
    while (blocks_.size() > 1 &&
           blocks_[1].timestamp != kNoTimestamp &&
           blocks_[1].timestamp <= target) {
        size_ -= blocks_.front().data.size() - blocks_.front().consumed;
        blocks_.pop_front();
    }
    Block& b = blocks_.front();
    size_ += b.consumed;
    b.consumed = 0;
    return true;
}

void SeekableBuffer::Clear() {
    blocks_.clear();
    size_ = 0;
}

}  // namespace XER::Media
