#include "media/demuxer/media_source.h"

#include "media/demuxer/chunk_demuxer.h"

namespace XER::Media {

std::int64_t MediaSource::Append(const std::uint8_t* data,
                                 std::size_t size) {
    if (!demuxer_ || state_ == ReadyState::kEnded) return -1;
    state_ = ReadyState::kOpen;
    return demuxer_->AppendData(data, size);
}

void MediaSource::EndOfStream() {
    state_ = ReadyState::kEnded;
    if (demuxer_) demuxer_->Flush();
}

}  // namespace XER::Media
