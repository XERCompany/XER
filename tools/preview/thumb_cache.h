#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace preview {

/// BGRA8 thumbnail ready for DrawFrameBgra. `ready=false`/empty `bgra` means
/// the source failed to decode — cached so it is not retried.
struct Thumbnail {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> bgra;
    bool ready = false;
};

/// Background thumbnail generator: decodes images off the UI thread and
/// caches small BGRA bitmaps keyed by file path. Single worker, deduplicated
/// request queue, bounded cache.
class ThumbCache {
public:
    explicit ThumbCache(int max_w = 128, int max_h = 88);
    ~ThumbCache();

    ThumbCache(const ThumbCache&) = delete;
    ThumbCache& operator=(const ThumbCache&) = delete;

    /// Enqueues `path` for generation (no-op when already cached/queued).
    void Request(const std::string& path);

    /// Copies the thumbnail into `out`; returns false while pending/failed.
    bool Get(const std::string& path, Thumbnail& out);

    /// Drops all cached/queued entries (e.g. folder changed).
    void Clear();

private:
    void WorkerLoop();
    Thumbnail Generate(const std::string& path) const;

    int max_w_;
    int max_h_;

    mutable std::mutex mu_;
    std::condition_variable cv_;
    std::deque<std::string> queue_;
    std::unordered_set<std::string> queued_;
    std::unordered_map<std::string, Thumbnail> cache_;
    std::thread worker_;
    std::atomic<bool> stop_{false};

    static constexpr std::size_t kMaxCached = 512;
};

}  // namespace preview
