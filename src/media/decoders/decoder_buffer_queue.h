/**
 * DecoderBufferQueue — pending coded units between DecoderStream and the
 * Decoder (design doc: decoders consume ReadResult units; the queue keeps
 * backpressure bounded so demuxer reads pause when the decoder falls
 * behind).
 */
#pragma once

#include <deque>

#include "media/demuxer/demuxer.h"

namespace XER::Media {

class DecoderBufferQueue {
public:
    explicit DecoderBufferQueue(std::size_t capacity = 32)
        : capacity_(capacity) {}

    // Returns false when full (backpressure — caller retries later).
    bool Push(DemuxerStream::ReadResult unit);
    bool Pop(DemuxerStream::ReadResult* out);

    std::size_t size() const { return queue_.size(); }
    std::size_t capacity() const { return capacity_; }
    bool full() const { return queue_.size() >= capacity_; }
    bool empty() const { return queue_.empty(); }
    void Clear() { queue_.clear(); }

private:
    std::size_t capacity_;
    std::deque<DemuxerStream::ReadResult> queue_;
};

}  // namespace XER::Media
