/**
 * SeekableBuffer — timestamped byte buffer for demuxer read-ahead
 * (design doc: buffers compressed data between DataSource and
 * StreamParser, tracks byte-stream position so backward seeks inside
 * the retained window don't re-read the source).
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "media/base/media_types.h"

namespace XER::Media {

class SeekableBuffer {
public:
    SeekableBuffer() = default;

    void Append(const std::uint8_t* data, std::size_t size,
                TimeTicks timestamp);
    // Reads up to |size| bytes; returns count. |timestamp_out| gets the
    // buffer position's timestamp (kNoTimestamp when unknown).
    std::size_t Read(std::uint8_t* out, std::size_t size,
                     TimeTicks* timestamp_out);
    std::size_t Peek(std::uint8_t* out, std::size_t size) const;
    void Skip(std::size_t size);

    // Backward seek within the retained window. False when the target
    // predates the buffer's earliest block.
    bool SeekBackward(TimeTicks target);
    void Clear();

    std::size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

private:
    struct Block {
        std::vector<std::uint8_t> data;
        TimeTicks timestamp = kNoTimestamp;
        std::size_t consumed = 0;
    };
    std::deque<Block> blocks_;
    std::size_t size_ = 0;
};

}  // namespace XER::Media
