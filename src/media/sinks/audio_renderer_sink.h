/**
 * AudioRendererSink — event-driven audio output interface (design doc).
 * The sink invokes its callback periodically when new audio frames are
 * required. Driven by a platform AudioOutputStream (media/audio) via a
 * SyncSocket + shared memory segment in the browser-process model.
 */
#pragma once

#include <functional>

namespace XER::Media {

// Periodic pull interface owned by the platform audio pipeline.
// The sink invokes callback() when it needs a new frame.
class AudioRendererSink {
public:
    using Callback = std::function<void()>;
    virtual ~AudioRendererSink() = default;
    virtual void Start(Callback cb) = 0;
    virtual void Stop() = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void SetVolume(double volume) = 0;  // 0.0..1.0
};

}  // namespace XER::Media
