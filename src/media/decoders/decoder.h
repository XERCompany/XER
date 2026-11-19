/**
 * XER::Media decoders. Each Audio/VideoRenderer owns a set of decoder
 * implementations (software via FFmpeg/libvpx, hardware via
 * AcceleratedVideoDecoder); the first one reporting success is used.
 * DecoderStream routes async DemuxerStream reads to the right decoder.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "media/demuxer/demuxer.h"
#include "media/base/media_types.h"

namespace XER::Media {

struct DecodedFrame {
    TimeTicks timestamp = kNoTimestamp;
    TimeTicks duration = 0;
    int width = 0;
    int height = 0;                      // video
    int sample_rate = 0;                 // audio
    int channels = 0;                    // audio
    std::vector<std::uint8_t> pixels;    // BGRA8 video / interleaved f32 audio
};

class Decoder {
public:
    using DecodeCallback = std::function<void(PipelineStatus, DecodedFrame)>;
    virtual ~Decoder() = default;
    virtual bool Initialize(const DemuxerStream& stream, std::string* error_out) = 0;
    virtual void Decode(const DemuxerStream::ReadResult& unit, DecodeCallback cb) = 0;
    virtual void Reset() = 0;
    virtual bool CanDecode(CodecId codec) const = 0;
};

class AudioDecoder : public Decoder {};
class VideoDecoder : public Decoder {};

}  // namespace XER::Media

#include "media/decoders/decoder_stream.h"
