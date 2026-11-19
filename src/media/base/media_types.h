/**
 * XER::Media — shared enums and value types for the media pipeline.
 *
 * The media stack is organised as:
 *   Demuxer -> DemuxerStream(s) -> DecoderStream -> Audio/VideoDecoder
 *           -> Audio/VideoRenderer -> Audio/VideoRendererSink
 * with a MediaPlayerImpl owning the pipeline (see MediaDecoder&Encoder -.txt).
 */
#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace XER::Media {

enum class StreamType : uint8_t { kAudio, kVideo, kText, kUnknown };

enum class CodecId : uint8_t {
    kUnknown,
    kAV1,
    kH264,
    kH265,
    kVP8,
    kVP9,
    kOpus,
    kAAC,
};

enum class PipelineStatus : uint8_t {
    kOk,
    kError,
    kAborted,
    kNotSupported,
};

// Rational-free timestamp in microseconds; media timestamps are always
// non-negative once normalised by MuxerTimestampAdapter.
using TimeTicks = std::int64_t;  // microseconds
constexpr TimeTicks kNoTimestamp = std::numeric_limits<TimeTicks>::min();

std::string CodecName(CodecId codec);
CodecId CodecFromName(const std::string& name);

}  // namespace XER::Media
