/**
 * VideoRendererImpl — concrete VideoRenderer: feeds decoded frames into a
 * VideoFrameCompositor; the sink's compositor callbacks pull the due frame
 * by TimeSource clock.
 */
#pragma once

#include <functional>
#include <memory>
#include <string>

#include "media/decoders/decoder_stream.h"
#include "media/renderers/video_frame_compositor.h"
#include "media/renderers/video_renderer.h"

namespace XER::Media {

class VideoRendererImpl final : public VideoRenderer {
public:
    explicit VideoRendererImpl(std::unique_ptr<Decoder> decoder);

    bool Initialize(Demuxer* demuxer, VideoRendererSink* sink,
                    TimeSource* time_source, std::string* error_out) override;
    void Play() override;
    void Pause() override;
    void Flush() override;

    // Frame due for compositing right now (sink callback path).
    const DecodedFrame* DueFrame() { return compositor_.CurrentFrame(); }
    VideoFrameCompositor* compositor() { return &compositor_; }

private:
    std::unique_ptr<Decoder> decoder_;
    std::unique_ptr<DecoderStream> stream_;
    VideoFrameCompositor compositor_;
    VideoRendererSink* sink_ = nullptr;
    bool playing_ = false;
};

}  // namespace XER::Media
