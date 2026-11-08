#pragma once
#include "frame_source.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include <deque>
#include <string>
#include <memory>

namespace video {

/// Concrete FrameSource backed by the FFmpeg bridge.
/// Probes the URI on Open(), lazily decodes frames on first NextFrame() call.
class FFmpegFrameSource : public FrameSource {
public:
    FFmpegFrameSource() = default;
    ~FFmpegFrameSource() override { Close(); }

    bool Open(const std::string& uri) override;
    void Close() override;
    bool IsOpen() const override { return is_open_; }

    std::shared_ptr<Frame> NextFrame() override;

    int    Width()  const override { return width_;  }
    int    Height() const override { return height_; }
    double Fps()    const override { return fps_;    }

    int64_t TotalFrames() const { return total_frames_; }

private:
    bool DecodeAll();
    std::shared_ptr<Frame> MakeFrameFromInfo(const XER::Bridge::FFmpeg::VideoFrameInfo& info);

    std::string uri_;
    int         stream_idx_{-1};
    int         width_{0}, height_{0};
    double      fps_{30.0};
    int64_t     total_frames_{0};
    bool        is_open_{false};
    bool        decoded_{false};

    std::deque<std::shared_ptr<Frame>> queue_;
};

}  // namespace video
