/**
 * DecoderStream — routes async DemuxerStream reads to the right Decoder
 * and forwards decoded frames to a renderer callback (design doc).
 */
#pragma once

#include <functional>

#include "media/decoders/decoder.h"

namespace XER::Media {

// Routes async reads from a DemuxerStream into a Decoder and forwards
// decoded frames to a renderer callback.
class DecoderStream {
public:
    DecoderStream(DemuxerStream* stream, Decoder* decoder);
    ~DecoderStream() = default;
    DecoderStream(const DecoderStream&) = delete;
    DecoderStream& operator=(const DecoderStream&) = delete;

    using FrameCallback = std::function<void(DecodedFrame)>;

    bool Initialize(std::string* error_out);
    // Starts the read->decode pump; frames are delivered until EOS or Stop().
    void Start(FrameCallback cb);
    void Stop();
    void Reset();

    DemuxerStream* stream() const { return stream_; }
    Decoder* decoder() const { return decoder_; }

private:
    void IssueRead();
    void OnRead(DemuxerStream::ReadResult result);
    void OnDecoded(PipelineStatus status, DecodedFrame frame);

    DemuxerStream* stream_;
    Decoder* decoder_;
    FrameCallback frame_cb_;
    bool running_ = false;
    bool read_pending_ = false;
    bool eos_ = false;
};

}  // namespace XER::Media
