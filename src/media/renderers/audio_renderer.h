/**
 * AudioRenderer — manages timing + rendering of the audio track via an
 * event-driven AudioRendererSink. Owns the master TimeSource the video
 * renderer syncs to (design doc).
 */
#pragma once

#include <string>

#include "media/demuxer/demuxer.h"
#include "media/sinks/audio_renderer_sink.h"
#include "media/renderers/time_source.h"

namespace XER::Media {

class AudioRenderer {
public:
    virtual ~AudioRenderer() = default;
    virtual bool Initialize(Demuxer* demuxer, AudioRendererSink* sink,
                            std::string* error_out) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Flush() = 0;
    virtual TimeSource* time_source() = 0;
};

}  // namespace XER::Media
