/**
 * XER::Media renderers (design doc). RendererImpl coordinates an
 * AudioRenderer and a VideoRenderer, each driving event-driven sinks.
 * VideoRenderer syncs to AudioRenderer through a TimeSource.
 *
 * Component headers: sinks/audio_renderer_sink.h,
 * sinks/video_renderer_sink.h, renderers/time_source.h,
 * renderers/audio_renderer.h, renderers/video_renderer.h,
 * renderers/video_frame_compositor.h.
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "media/decoders/decoder.h"
#include "media/demuxer/demuxer.h"
#include "media/base/media_types.h"
#include "media/renderers/audio_renderer.h"
#include "media/renderers/time_source.h"
#include "media/renderers/video_frame_compositor.h"
#include "media/renderers/video_renderer.h"
#include "media/sinks/audio_renderer_sink.h"
#include "media/sinks/video_renderer_sink.h"

namespace XER::Media {

// Top-level playback coordinator created by MediaPlayerImpl.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual bool Initialize(Demuxer* demuxer,
                            AudioRendererSink* audio_sink,
                            VideoRendererSink* video_sink,
                            std::string* error_out) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual bool Seek(TimeTicks position, std::string* error_out) = 0;
    virtual void SetVolume(double volume) = 0;
    virtual TimeTicks media_time() const = 0;
};

// Provides decoder implementations in preference order; the first decoder
// reporting CanDecode + successful Initialize wins (hardware first).
class RendererFactory {
public:
    virtual ~RendererFactory() = default;
    virtual std::vector<std::unique_ptr<AudioDecoder>> CreateAudioDecoders() = 0;
    virtual std::vector<std::unique_ptr<VideoDecoder>> CreateVideoDecoders() = 0;
};

}  // namespace XER::Media
