/**
 * VideoRenderer — manages timing + rendering of the video track via an
 * event-driven VideoRendererSink; syncs to the audio renderer through a
 * TimeSource (design doc).
 */
#pragma once

#include <string>

#include "media/demuxer/demuxer.h"
#include "media/sinks/video_renderer_sink.h"
#include "media/renderers/time_source.h"

namespace XER::Media {

class VideoRenderer {
public:
    virtual ~VideoRenderer() = default;
    virtual bool Initialize(Demuxer* demuxer, VideoRendererSink* sink,
                            TimeSource* time_source, std::string* error_out) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Flush() = 0;
};

}  // namespace XER::Media
