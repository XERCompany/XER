/**
 * VideoRendererSink — event-driven video output interface (design doc).
 * Driven by async callbacks issued by the compositor to
 * VideoFrameCompositor.
 */
#pragma once

#include <functional>

namespace XER::Media {

class VideoRendererSink {
public:
    using Callback = std::function<void()>;
    virtual ~VideoRendererSink() = default;
    virtual void Start(Callback cb) = 0;
    virtual void Stop() = 0;
};

}  // namespace XER::Media
