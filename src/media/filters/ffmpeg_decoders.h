/**
 * FFmpeg-backed decoder implementations + the RendererFactory that
 * produces them. The FFmpeg bridge already decodes inside the demuxer
 * stream, so these decoders are pass-through adapters that convert
 * DemuxerStream::ReadResult into DecodedFrame — keeping the
 * decoder-preference-order contract of RendererFactory.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/decoders/decoder.h"
#include "media/renderers/renderer.h"

namespace XER::Media {

class FFmpegVideoDecoder final : public VideoDecoder {
public:
    bool Initialize(const DemuxerStream& stream, std::string* error_out) override;
    void Decode(const DemuxerStream::ReadResult& unit, DecodeCallback cb) override;
    void Reset() override {}
    bool CanDecode(CodecId codec) const override;
};

class FFmpegAudioDecoder final : public AudioDecoder {
public:
    bool Initialize(const DemuxerStream& stream, std::string* error_out) override;
    void Decode(const DemuxerStream::ReadResult& unit, DecodeCallback cb) override;
    void Reset() override {}
    bool CanDecode(CodecId codec) const override;
};

// Software-decoder factory (FFmpeg via the universal decoder module).
// Hardware decoders from YA.MediaGpu should precede these in preference
// order when registered.
class FFmpegRendererFactory final : public RendererFactory {
public:
    std::vector<std::unique_ptr<AudioDecoder>> CreateAudioDecoders() override;
    std::vector<std::unique_ptr<VideoDecoder>> CreateVideoDecoders() override;
};

}  // namespace XER::Media
