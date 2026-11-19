#include "media/decoders/decoder_buffer_queue.h"

namespace XER::Media {

bool DecoderBufferQueue::Push(DemuxerStream::ReadResult unit) {
    if (full()) return false;
    queue_.push_back(std::move(unit));
    return true;
}

bool DecoderBufferQueue::Pop(DemuxerStream::ReadResult* out) {
    if (!out || queue_.empty()) return false;
    *out = std::move(queue_.front());
    queue_.pop_front();
    return true;
}

}  // namespace XER::Media
