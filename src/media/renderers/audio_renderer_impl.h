/**
 * AudioRendererImpl — concrete AudioRenderer: drives a DecoderStream of
 * decoded audio into the sink on demand, owns the master TimeSource.
 */
#pragma once

#include <deque>
#include <memory>
#include <mutex>
#include <string>

#include "media/base/audio_bus.h"
#include "media/base/audio_fifo.h"
#include "media/decoders/decoder_stream.h"
#include "media/renderers/audio_renderer.h"

namespace XER::Media {

class AudioRendererImpl final : public AudioRenderer {
public:
    explicit AudioRendererImpl(std::unique_ptr<Decoder> decoder);

    bool Initialize(Demuxer* demuxer, AudioRendererSink* sink,
                    std::string* error_out) override;
    void Play() override;
    void Pause() override;
    void Flush() override;
    TimeSource* time_source() override { return &clock_; }

    // Decoded audio delivered by the decoder stream; consumed by
    // PopFrame() on sink callbacks.
    bool PopFrame(DecodedFrame* out);

    // Sink device pull: fills |dst| with up to |frames| planar f32 from
    // the decoded-frame queue via AudioFifo (block-size decoupling).
    // Returns frames actually written (device zero-fills the rest).
    int RenderInto(AudioBus* dst, int frames);

private:
    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<DecoderStream> stream_;
    WallClockTimeSource clock_;
    AudioRendererSink* sink_ = nullptr;
    std::mutex mu_;
    std::deque<DecodedFrame> queue_;
    AudioFifo fifo_{2, 96000};  // ~2s stereo @48k
    bool playing_ = false;
};

}  // namespace XER::Media
