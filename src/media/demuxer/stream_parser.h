/**
 * StreamParser — incremental container parser for ChunkDemuxer
 * (design doc: parses appended init/media segments; emits tracks on
 * init and coded units on each media segment). WebM/MP4 impls plug in
 * via ChunkDemuxerImpl::SetInitParser + a unit callback.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "media/demuxer/media_track.h"

namespace XER::Media {

class StreamParser {
public:
    struct ParseResult {
        bool ok = false;
        std::size_t consumed = 0;   // bytes consumed from the input
        bool is_init = false;       // parsed an init segment
        std::vector<MediaTrack> tracks;
        std::string error;
    };

    virtual ~StreamParser() = default;
    // Parses as much of |data| as possible. Never blocks; returns the
    // prefix consumed so callers can rotate their byte queue.
    virtual ParseResult Parse(const std::uint8_t* data,
                              std::size_t size) = 0;
    virtual void Reset() = 0;

    // Detects the container from magic bytes; returns a parser name
    // ("webm", "mp4", "raw") without instantiating heavy parsers.
    static const char* Sniff(const std::uint8_t* data, std::size_t size);
};

}  // namespace XER::Media
