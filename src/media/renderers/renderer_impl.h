/**
 * RendererImpl — the typical XER::Media::Renderer: owns and coordinates a
 * concrete AudioRendererImpl and VideoRendererImpl (design doc).
 */
#pragma once

#include <memory>
#include <string>

#include "media/renderers/audio_renderer_impl.h"
#include "media/renderers/renderer.h"
#include "media/renderers/video_renderer_impl.h"

namespace XER::Media {

class RendererImpl final : public Renderer {
public:
    RendererImpl(std::unique_ptr<Decoder> audio_decoder,
                 std::unique_ptr<Decoder> video_decoder);

    bool Initialize(Demuxer* demuxer, AudioRendererSink* audio_sink,
                    VideoRendererSink* video_sink,
                    std::string* error_out) override;
    void Play() override;
    void Pause() override;
    void Stop() override;
    bool Seek(TimeTicks position, std::string* error_out) override;
    void SetVolume(double volume) override;
    TimeTicks media_time() const override;

    AudioRenderer* audio_renderer() { return audio_.get(); }
    VideoRenderer* video_renderer() { return video_.get(); }

private:
    std::unique_ptr<AudioRendererImpl> audio_;
    std::unique_ptr<VideoRendererImpl> video_;
    Demuxer* demuxer_ = nullptr;
    AudioRendererSink* audio_sink_ = nullptr;
};

}  // namespace XER::Media
