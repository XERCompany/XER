#pragma once
#include "frame.h"
#include "frame_cache.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace video {

/// Writes frames to cache, disk, or video file using FFmpeg bridge.
class FrameExporter {
public:
    using FrameStreamProvider = std::function<bool(size_t frame_index,
                                                   std::shared_ptr<Frame>* out_frame,
                                                   std::string* error_out)>;

    explicit FrameExporter(FrameCache* cache = nullptr);

    bool ToCache(const std::string& key, std::shared_ptr<Frame> frame);

    /// Save single frame to file (PNG by default via FFmpeg).
    bool ToFile(const std::string& path, const Frame& frame);

    /// Export a sequence of frames to a video file.
    bool ExportFrames(const std::string& path,
                      const std::vector<std::shared_ptr<Frame>>& frames,
                      const std::string& codec = "libx264",
                      int fps = 30);

    bool ExportFrameStream(const std::string& path,
                           size_t frame_count,
                           int width,
                           int height,
                           const std::string& codec,
                           int fps,
                           const FrameStreamProvider& provider);

    void SetCache(FrameCache* cache) { cache_ = cache; }

private:
    FrameCache* cache_{nullptr};
};

}  // namespace video