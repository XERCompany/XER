#include "frame_importer.h"
#include "ffmpeg_frame_source.h"
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"
#include "flux/core/logger.h"

namespace video {

static XER::Flux::Core::Logger& Log() {
    static XER::Flux::Core::Logger logger;
    return logger;
}

FrameImporter::FrameImporter(FrameCache* cache) : cache_(cache) {}

std::shared_ptr<Frame> FrameImporter::FromCache(const std::string& key) {
    if (!cache_) return nullptr;
    return cache_->Get(key);
}

std::shared_ptr<Frame> FrameImporter::FromFile(const std::string& path) {
    if (!XER::Bridge::FFmpeg::IsAvailable()) {
        Log().Error("FrameImporter", "FFmpeg bridge not available");
        return nullptr;
    }
    // Use FFmpegFrameSource to decode the first video frame
    FFmpegFrameSource src;
    if (!src.Open(path)) {
        Log().Error("FrameImporter", "Cannot open: " + path);
        return nullptr;
    }
    auto frame = src.NextFrame();
    if (frame) Log().Info("FrameImporter", "Loaded frame from " + path);
    return frame;
}

std::vector<std::shared_ptr<Frame>> FrameImporter::AllFramesFromFile(const std::string& path) {
    std::vector<std::shared_ptr<Frame>> result;
    if (!XER::Bridge::FFmpeg::IsAvailable()) return result;

    FFmpegFrameSource src;
    if (!src.Open(path)) return result;
    while (true) {
        auto f = src.NextFrame();
        if (!f) break;
        result.push_back(std::move(f));
    }
    Log().Info("FrameImporter",
               "Loaded " + std::to_string(result.size()) + " frames from " + path);
    return result;
}

std::shared_ptr<Frame> FrameImporter::FromFileOrCache(const std::string& path) {
    if (cache_) {
        auto cached = cache_->Get(path);
        if (cached) {
            Log().Debug("FrameImporter", "Cache hit: " + path);
            return cached;
        }
    }
    auto frame = FromFile(path);
    if (frame && cache_) cache_->Store(path, frame);
    return frame;
}

}  // namespace video