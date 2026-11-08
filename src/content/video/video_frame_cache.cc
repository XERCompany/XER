#include "video_frame_cache.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

#include "cache/cache_manager.h"
#include "helper/string_utils.h"

namespace XER::Content::Video {

namespace {

constexpr std::uint32_t kPosterMagic = 0x52564658;  // 'XVFR' little-endian
constexpr std::uint32_t kPosterVersion = 1;

// FNV-1a 64 — stable across runs, no dependency on std::hash implementation.
std::uint64_t Fnv1a(const std::string& s) {
    std::uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

std::string Utf8Path(const std::filesystem::path& p) {
    return XER::Helper::String::WideToUtf8(p.wstring());
}

std::filesystem::path PosterDir() {
    return XER::Cache::CacheManager::Instance().Persistent().root() /
           "video_cache" / "posters";
}

}  // namespace

VideoFrameCache::VideoFrameCache() = default;

void VideoFrameCache::Remember(const std::string& video_path,
                               VideoFramePtr frame) {
    if (video_path.empty() || frame == nullptr) return;
    std::lock_guard<std::mutex> lock(mutex_);
    memory_[video_path] = MemoryEntry{std::move(frame), ++clock_};
    if (memory_.size() > kMemoryLimit) {
        // Evict the least-recently-touched entry.
        auto oldest = memory_.begin();
        for (auto it = memory_.begin(); it != memory_.end(); ++it) {
            if (it->second.tick < oldest->second.tick) oldest = it;
        }
        memory_.erase(oldest);
    }
}

VideoFramePtr VideoFrameCache::Recall(const std::string& video_path) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = memory_.find(video_path);
    return it != memory_.end() ? it->second.frame : nullptr;
}

void VideoFrameCache::Forget(const std::string& video_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    memory_.erase(video_path);
}

std::string VideoFrameCache::PosterPath(const std::string& video_path) {
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.xervid",
                  static_cast<unsigned long long>(HashPath(video_path)));
    return Utf8Path(PosterDir() / name);
}

std::uint64_t VideoFrameCache::HashPath(const std::string& video_path) {
    return Fnv1a(video_path);
}

bool VideoFrameCache::StorePoster(const std::string& video_path,
                                  const VideoFrame& frame) {
    if (video_path.empty() || frame.bgra.empty() || frame.width <= 0 ||
        frame.height <= 0) {
        return false;
    }

    // Downscale to kPosterMaxDim on the longest side (box filter) so the
    // cache file stays small — a poster only needs to be recognizable.
    int out_w = frame.width;
    int out_h = frame.height;
    const double pts_seconds = frame.position_seconds;
    const std::vector<std::uint8_t>* src = &frame.bgra;
    std::vector<std::uint8_t> scaled;
    const int longest = std::max(frame.width, frame.height);
    if (longest > kPosterMaxDim) {
        const double scale =
            static_cast<double>(kPosterMaxDim) / longest;
        out_w = std::max(1, static_cast<int>(frame.width * scale));
        out_h = std::max(1, static_cast<int>(frame.height * scale));
        scaled.resize(static_cast<std::size_t>(out_w) * out_h * 4);
        for (int y = 0; y < out_h; ++y) {
            const int sy = std::min(frame.height - 1,
                                    static_cast<int>(y / scale));
            for (int x = 0; x < out_w; ++x) {
                const int sx = std::min(frame.width - 1,
                                        static_cast<int>(x / scale));
                const std::uint8_t* s =
                    frame.bgra.data() +
                    (static_cast<std::size_t>(sy) * frame.width + sx) * 4;
                std::uint8_t* d =
                    scaled.data() +
                    (static_cast<std::size_t>(y) * out_w + x) * 4;
                d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
            }
        }
        src = &scaled;
    }

    const std::filesystem::path dir = PosterDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;

    std::ofstream file(std::filesystem::path(
                           XER::Helper::String::Utf8ToWide(
                               PosterPath(video_path))),
                       std::ios::binary | std::ios::trunc);
    if (!file) return false;

    const std::uint32_t header[4] = {
        kPosterMagic, kPosterVersion,
        static_cast<std::uint32_t>(out_w),
        static_cast<std::uint32_t>(out_h)};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    const std::uint64_t pts_bits =
        static_cast<std::uint64_t>(pts_seconds * 1000000.0);
    file.write(reinterpret_cast<const char*>(&pts_bits), sizeof(pts_bits));
    file.write(reinterpret_cast<const char*>(src->data()),
               static_cast<std::streamsize>(src->size()));
    return file.good();
}

bool VideoFrameCache::LoadPoster(const std::string& video_path,
                                 VideoFrame* out) const {
    if (out == nullptr) return false;
    const std::string path = PosterPath(video_path);
    std::ifstream file(std::filesystem::path(
                           XER::Helper::String::Utf8ToWide(path)),
                       std::ios::binary);
    if (!file) return false;

    std::uint32_t header[4]{};
    file.read(reinterpret_cast<char*>(header), sizeof(header));
    if (!file || header[0] != kPosterMagic || header[1] != kPosterVersion ||
        header[2] == 0 || header[3] == 0) {
        return false;
    }
    std::uint64_t pts_bits = 0;
    file.read(reinterpret_cast<char*>(&pts_bits), sizeof(pts_bits));

    VideoFrame frame;
    frame.width = static_cast<int>(header[2]);
    frame.height = static_cast<int>(header[3]);
    frame.position_seconds = static_cast<double>(pts_bits) / 1000000.0;
    const std::size_t expected =
        static_cast<std::size_t>(frame.width) * frame.height * 4;
    frame.bgra.resize(expected);
    file.read(reinterpret_cast<char*>(frame.bgra.data()),
              static_cast<std::streamsize>(expected));
    if (!file) return false;

    *out = std::move(frame);
    return true;
}

bool VideoFrameCache::HasPoster(const std::string& video_path) const {
    std::error_code ec;
    return std::filesystem::exists(
        std::filesystem::path(
            XER::Helper::String::Utf8ToWide(PosterPath(video_path))), ec);
}

}  // namespace XER::Content::Video
