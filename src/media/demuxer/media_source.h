/**
 * MediaSource — the MSE-side object producing chunks for a ChunkDemuxer
 * (design doc: SourceBuffers append into it; it owns duration and the
 * ready-state machine).
 */
#pragma once

#include <string>

#include "media/base/media_types.h"

namespace XER::Media {

class ChunkDemuxer;

class MediaSource {
public:
    enum class ReadyState : std::uint8_t { kClosed, kOpen, kEnded };

    explicit MediaSource(ChunkDemuxer* demuxer = nullptr)
        : demuxer_(demuxer) {}

    void SetDemuxer(ChunkDemuxer* demuxer) { demuxer_ = demuxer; }
    ChunkDemuxer* demuxer() const { return demuxer_; }

    void SetReadyState(ReadyState s) { state_ = s; }
    ReadyState ready_state() const { return state_; }

    void SetDuration(TimeTicks d) { duration_ = d; }
    TimeTicks duration() const { return duration_; }

    // Appends a chunk to the demuxer; returns bytes consumed or -1.
    std::int64_t Append(const std::uint8_t* data, std::size_t size);
    void EndOfStream();

private:
    ChunkDemuxer* demuxer_;
    ReadyState state_ = ReadyState::kClosed;
    TimeTicks duration_ = kNoTimestamp;
};

}  // namespace XER::Media
