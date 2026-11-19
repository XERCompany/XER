/**
 * FrameCacheSink — VideoRendererSink persisting each composited frame
 * to video::ClipFrameCache (src/video on top of
 * XER::Cache::PersistentStorage). The offscreen/capture path from the
 * design doc: frames land on disk under a per-clip key, replayable
 * through ReadFrame().
 */
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "media/decoders/decoder.h"
#include "media/sinks/video_renderer_sink.h"

namespace video {
class ClipFrameCache;
}

namespace XER::Media {

class FrameCacheSink final : public VideoRendererSink {
public:
    FrameCacheSink(std::filesystem::path root, std::string key);
    ~FrameCacheSink() override;

    // VideoRendererSink — the callback fires per accepted frame.
    void Start(Callback cb) override;
    void Stop() override;

    // Called by the compositor with the current presentation frame.
    bool Render(const DecodedFrame& frame, std::string* error_out);

    std::size_t frames_written() const { return frames_written_; }

private:
    std::unique_ptr<video::ClipFrameCache> cache_;
    Callback cb_;
    std::size_t frames_written_ = 0;
    bool running_ = false;
};

}  // namespace XER::Media
