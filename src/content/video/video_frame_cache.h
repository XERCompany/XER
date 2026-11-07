#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "video_frame.h"

namespace XER::Content::Video {

// Frame cache for fast preview rendering:
//
//  * MemoryCache — the most recent frame per source path, so switching
//    tabs/projects and coming back shows the last picture instantly
//    without waiting for the decoder.
//  * PosterCache — a downscaled still frame persisted under the app cache
//    (<persistent>/video_cache/posters/<hash>.xervid), written once per
//    file. Reopening a video shows its poster immediately while the
//    decoder spins up — the "important cache files" that make repeat
//    renders fast.
//
// Thread-safe; all methods may be called from any thread.
class VideoFrameCache {
public:
    VideoFrameCache();

    // Memory cache (bounded — oldest entries are evicted past the cap).
    void Remember(const std::string& video_path, VideoFramePtr frame);
    VideoFramePtr Recall(const std::string& video_path) const;
    void Forget(const std::string& video_path);

    // Disk poster cache. StorePoster() downscales frames whose longest
    // side exceeds kPosterMaxDim before writing. Returns false on I/O
    // failure (cache is best-effort — never fatal to playback).
    bool StorePoster(const std::string& video_path, const VideoFrame& frame);
    bool LoadPoster(const std::string& video_path, VideoFrame* out) const;
    bool HasPoster(const std::string& video_path) const;

private:
    struct MemoryEntry {
        VideoFramePtr frame;
        std::uint64_t tick = 0;
    };

    static std::string PosterPath(const std::string& video_path);
    static std::uint64_t HashPath(const std::string& video_path);

    mutable std::mutex mutex_;
    std::unordered_map<std::string, MemoryEntry> memory_;
    std::uint64_t clock_ = 0;
    static constexpr std::size_t kMemoryLimit = 12;
    static constexpr int kPosterMaxDim = 512;
};

}  // namespace XER::Content::Video
