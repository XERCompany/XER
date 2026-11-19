/**
 * XER::Media::Demuxer — container demultiplexing (design doc:
 * MediaDecoder&Encoder). Implementations: FFmpeg-backed Demuxer
 * (src/wrapper/ffmpeg) for src= playback and ChunkDemuxer for MSE.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media/base/media_types.h"

namespace XER::Media {

// A single elementary stream exposed by a Demuxer. Reads are asynchronous:
// the caller issues Read() and decoded-ready units arrive via callback.
class DemuxerStream {
public:
    struct ReadResult {
        bool ok = false;
        bool end_of_stream = false;
        std::vector<std::uint8_t> data;
        TimeTicks timestamp = kNoTimestamp;
        TimeTicks duration = 0;
    };
    using ReadCallback = std::function<void(ReadResult)>;

    virtual ~DemuxerStream() = default;

    virtual StreamType type() const = 0;
    virtual CodecId codec() const = 0;
    // Issues one async read; the callback fires once data is available or EOS
    // is reached. Only one read may be outstanding per stream.
    virtual void Read(ReadCallback cb) = 0;
};

class Demuxer {
public:
    virtual ~Demuxer() = default;

    // Opens a container at |path|. Returns false and fills |error_out| on
    // failure. After Open(), streams() is populated.
    virtual bool Open(const std::string& path, std::string* error_out) = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    virtual const std::vector<DemuxerStream*>& streams() const = 0;
    DemuxerStream* GetStream(StreamType type) const {
        for (auto* s : streams())
            if (s->type() == type) return s;
        return nullptr;
    }

    virtual bool Seek(TimeTicks position, std::string* error_out) = 0;
    virtual TimeTicks duration() const = 0;
};

// MSE variant: the host feeds container bytes incrementally instead of
// reading from a URI.
class ChunkDemuxer : public Demuxer {
public:
    // Appends raw container bytes (webm/mp4 segment). Returns bytes consumed
    // or negative on parse error.
    virtual std::int64_t AppendData(const std::uint8_t* data, std::size_t size) = 0;
    virtual void Flush() = 0;
    virtual void ResetParserState() = 0;
};

}  // namespace XER::Media
