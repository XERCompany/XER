#include "thumb_cache.h"

#include "image/core/image_buffer.h"
#include "image/io/image_loader.h"
#include "image/tools/transform_tool.h"
#include "photo_browser.h"  // NormalizeToRGBA8

namespace preview {

ThumbCache::ThumbCache(int max_w, int max_h)
    : max_w_(max_w), max_h_(max_h),
      worker_([this] { WorkerLoop(); }) {}

ThumbCache::~ThumbCache() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        stop_ = true;
        queue_.clear();
        queued_.clear();
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void ThumbCache::Request(const std::string& path) {
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (cache_.count(path) || queued_.count(path)) return;
        queued_.insert(path);
        queue_.push_back(path);
    }
    cv_.notify_one();
}

bool ThumbCache::Get(const std::string& path, Thumbnail& out) {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = cache_.find(path);
    if (it == cache_.end() || !it->second.ready) return false;
    out = it->second;
    return true;
}

void ThumbCache::Clear() {
    {
        std::lock_guard<std::mutex> lock(mu_);
        cache_.clear();
        queue_.clear();
        queued_.clear();
    }
    cv_.notify_all();
}

void ThumbCache::WorkerLoop() {
    for (;;) {
        std::string path;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [this] {
                return stop_ || !queue_.empty();
            });
            if (stop_) return;
            path = std::move(queue_.front());
            queue_.pop_front();
        }

        Thumbnail thumb = Generate(path);

        {
            std::lock_guard<std::mutex> lock(mu_);
            queued_.erase(path);
            if (cache_.size() >= kMaxCached) cache_.clear();
            cache_.emplace(path, std::move(thumb));
        }
    }
}

Thumbnail ThumbCache::Generate(const std::string& path) const {
    Thumbnail result;

    image::ImageLoader loader;
    image::ImageDescriptor desc;
    auto raw = loader.Load(path, desc);
    if (!raw || !raw->IsValid()) return result;

    image::ImageBuffer rgba;
    if (!NormalizeToRGBA8(*raw, rgba)) return result;

    // Downscale preserving aspect ratio into the thumbnail box.
    image::ImageBuffer small = image::TransformTool::FitInto(rgba, max_w_, max_h_);
    if (!small.IsValid()) small = std::move(rgba);

    const int w = small.Width();
    const int h = small.Height();
    const std::size_t px = static_cast<std::size_t>(w) * h;
    result.bgra.resize(px * 4);
    const uint8_t* s = small.Data();
    uint8_t* d = result.bgra.data();
    for (std::size_t i = 0; i < px; ++i) {
        d[i * 4 + 0] = s[i * 4 + 2];
        d[i * 4 + 1] = s[i * 4 + 1];
        d[i * 4 + 2] = s[i * 4 + 0];
        d[i * 4 + 3] = s[i * 4 + 3];
    }
    result.width = w;
    result.height = h;
    result.ready = true;
    return result;
}

}  // namespace preview
