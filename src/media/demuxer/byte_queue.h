/**
 * ByteQueue — append/peek/consume byte ring used by ChunkDemuxer to hold
 * MSE segments until the parser can extract complete units.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::Media {

class ByteQueue {
public:
    explicit ByteQueue(std::size_t initial_capacity = 1 << 16);

    void Append(const std::uint8_t* data, std::size_t size);
    std::size_t Peek(std::uint8_t* out, std::size_t size) const;
    void Consume(std::size_t size);
    void Clear();

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    // Offset of the byte that starts the queue (absolute position of the
    // first buffered byte in the appended stream).
    std::uint64_t head_offset() const { return head_offset_; }

private:
    std::vector<std::uint8_t> buf_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::uint64_t head_offset_ = 0;
};

}  // namespace XER::Media
