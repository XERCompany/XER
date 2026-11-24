// render/video_frame.cc — see video_frame.h for the design notes.
#include "video_frame.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace XER::VideoEditor::Render {
namespace {

// Per-format plane geometry for a coded w*h: fills stride+size for up
// to kMaxPlanes Plane entries (ptr stays null — callers point them at
// real storage) and returns the total byte count, or 0 for an
// unsupported format or size.
std::size_t ComputePlaneLayout(VideoPixelFormat format, int w, int h,
                               VideoFrame::Plane* out) {
    if (w <= 0 || h <= 0) {
        return 0;
    }
    switch (format) {
        case VideoPixelFormat::kBGRA8:
        case VideoPixelFormat::kRGBA8: {
            out[0].stride = w * 4;
            out[0].size = static_cast<std::size_t>(out[0].stride) * h;
            return out[0].size;
        }
        case VideoPixelFormat::kNV12: {
            // Y plane w*h; interleaved UV plane stride must be even —
            // round the width up to a chroma pair.
            const int uv_stride = ((w + 1) / 2) * 2;
            const int uv_h = (h + 1) / 2;
            out[0].stride = w;
            out[0].size = static_cast<std::size_t>(w) * h;
            out[1].stride = uv_stride;
            out[1].size = static_cast<std::size_t>(uv_stride) * uv_h;
            return out[0].size + out[1].size;
        }
        case VideoPixelFormat::kI420: {
            const int c_stride = (w + 1) / 2;
            const int c_h = (h + 1) / 2;
            out[0].stride = w;
            out[0].size = static_cast<std::size_t>(w) * h;
            out[1].stride = c_stride;
            out[1].size = static_cast<std::size_t>(c_stride) * c_h;
            out[2].stride = c_stride;
            out[2].size = static_cast<std::size_t>(c_stride) * c_h;
            return out[0].size + out[1].size + out[2].size;
        }
    }
    return 0;
}

// Full-frame visible rect when the caller passes an empty one (our
// callers mean "unpadded" in practice — the normalization
// media::VideoFrameLayout effectively applies for unpadded buffers).
VideoRect NormalizeVisible(VideoRect visible, VideoSize coded) {
    if (visible.IsEmpty()) {
        return VideoRect{0, 0, coded.width, coded.height};
    }
    return visible;
}

// Points a layout's null plane pointers into `base` at successive
// plane offsets.
void BindPlanes(VideoFrame::Plane* planes, int count,
                std::uint8_t* base) {
    std::size_t offset = 0;
    for (int i = 0; i < count; ++i) {
        planes[i].ptr = base + offset;
        offset += planes[i].size;
    }
}

// Standalone owned-memory frame (the VideoFramePool-free path).
class OwnedVideoFrame final : public VideoFrame {
public:
    // Allocates the buffer for `coded` in `format`.
    OwnedVideoFrame(VideoPixelFormat format, VideoSize coded,
                    VideoRect visible, std::int64_t ts,
                    std::int64_t dur)
        : pixels_(BytesRequired(format, coded.width, coded.height)) {
        Plane planes[kMaxPlanes]{};
        const int n = NumPlanes(format);
        ComputePlaneLayout(format, coded.width, coded.height, planes);
        BindPlanes(planes, n, pixels_.data());
        InitCpu(format, coded, visible, ts, dur, planes, n);
    }
    // Adopts the caller's buffer (single-plane formats only — checked
    // by WrapOwnedMemory before construction).
    OwnedVideoFrame(VideoPixelFormat format, VideoSize coded,
                    VideoRect visible, std::int64_t ts, std::int64_t dur,
                    std::vector<std::uint8_t>&& pixels)
        : pixels_(std::move(pixels)) {
        Plane plane{};
        plane.stride = coded.width * 4;
        plane.size = pixels_.size();
        plane.ptr = pixels_.data();
        InitCpu(format, coded, visible, ts, dur, &plane, 1);
    }

private:
    std::vector<std::uint8_t> pixels_;
};

// Texture-backed frame — the WrapSharedImage analogue. Owns no pixels;
// the VideoTexture descriptor + release callback carry the whole
// contract (slice 2+ fills producers).
class TextureVideoFrame final : public VideoFrame {
public:
    TextureVideoFrame(VideoPixelFormat format, VideoSize coded,
                      VideoRect visible, std::int64_t ts,
                      std::int64_t dur, VideoTexture texture,
                      ReleaseCallback mailbox_release)
        : mailbox_release_(std::move(mailbox_release)) {
        InitTexture(format, coded, visible, ts, dur, texture);
    }
    ~TextureVideoFrame() override {
        // Fired on last Release — the mailbox-release / keyed-mutex
        // retirement hook (Chromium's ReleaseMailboxCB).
        if (mailbox_release_) {
            mailbox_release_();
        }
    }

private:
    ReleaseCallback mailbox_release_;
};

// Frame whose storage comes from (and returns to) a
// VideoFramePoolCore. Holds the storage strongly (pixel pointers stay
// valid for the frame's life) and the pool core weakly — a dead pool
// simply means the storage frees here instead of recycling.
class PooledVideoFrame final : public VideoFrame {
public:
    PooledVideoFrame(
        VideoPixelFormat format, VideoSize coded, VideoRect visible,
        std::int64_t ts, std::int64_t dur,
        std::shared_ptr<std::vector<std::uint8_t>> storage,
        std::weak_ptr<detail::VideoFramePoolCore> core,
        std::uint64_t key)
        : storage_(std::move(storage)),
          core_(std::move(core)),
          key_(key) {
        Plane planes[kMaxPlanes]{};
        const int n = NumPlanes(format);
        ComputePlaneLayout(format, coded.width, coded.height, planes);
        BindPlanes(planes, n, storage_->data());
        InitCpu(format, coded, visible, ts, dur, planes, n);
    }
    ~PooledVideoFrame() override {
        if (auto core = core_.lock()) {
            VideoFramePool::ReturnStorage(*core, key_,
                                          std::move(storage_));
        }
        // Weak core expired (pool destroyed): storage_ frees normally.
    }

private:
    std::shared_ptr<std::vector<std::uint8_t>> storage_;
    std::weak_ptr<detail::VideoFramePoolCore> core_;
    std::uint64_t key_;
};

}  // namespace

// ---------------------------------------------------------------------
// detail::VideoFramePoolCore
// ---------------------------------------------------------------------

namespace detail {

struct VideoFramePoolCore {
    explicit VideoFramePoolCore(VideoFramePool::Options o) : options(o) {}

    // Pack (format,w,h) into a map key — format < 256, dims < 2^24.
    static std::uint64_t MakeKey(VideoPixelFormat format, int w, int h) {
        return (static_cast<std::uint64_t>(format) << 48) |
               (static_cast<std::uint64_t>(w & 0xFFFFFF) << 24) |
               static_cast<std::uint64_t>(h & 0xFFFFFF);
    }

    std::size_t CountIdle() const {
        std::size_t n = 0;
        for (const auto& [k, list] : idle) {
            n += list.size();
        }
        return n;
    }

    VideoFramePool::Options options;
    std::mutex mutex;
    std::unordered_map<
        std::uint64_t,
        std::deque<std::shared_ptr<std::vector<std::uint8_t>>>>
        idle;
    std::size_t idle_bytes = 0;
    VideoFramePool::Stats stats;
};

}  // namespace detail

// ---------------------------------------------------------------------
// VideoFrame
// ---------------------------------------------------------------------

int VideoFrame::stride(int plane) const {
    return (plane >= 0 && plane < plane_count_) ? planes_[plane].stride
                                                : 0;
}

const std::uint8_t* VideoFrame::data(int plane) const {
    return (plane >= 0 && plane < plane_count_) ? planes_[plane].ptr
                                                : nullptr;
}

std::uint8_t* VideoFrame::data(int plane) {
    return (plane >= 0 && plane < plane_count_) ? planes_[plane].ptr
                                                : nullptr;
}

std::size_t VideoFrame::plane_size(int plane) const {
    return (plane >= 0 && plane < plane_count_) ? planes_[plane].size
                                                : 0;
}

void VideoFrame::InitCpu(VideoPixelFormat format, VideoSize coded_size,
                         VideoRect visible_rect,
                         std::int64_t timestamp_us,
                         std::int64_t duration_us, const Plane* planes,
                         int plane_count) {
    format_ = format;
    storage_ = VideoFrameStorage::kOwnedMemory;
    coded_size_ = coded_size;
    visible_rect_ = NormalizeVisible(visible_rect, coded_size);
    timestamp_us_ = timestamp_us;
    duration_us_ = duration_us;
    plane_count_ = std::min(plane_count, static_cast<int>(kMaxPlanes));
    for (int i = 0; i < plane_count_; ++i) {
        planes_[i] = planes[i];
    }
}

void VideoFrame::InitTexture(VideoPixelFormat format,
                             VideoSize coded_size,
                             VideoRect visible_rect,
                             std::int64_t timestamp_us,
                             std::int64_t duration_us,
                             VideoTexture texture) {
    format_ = format;
    storage_ = VideoFrameStorage::kOpaqueTexture;
    coded_size_ = coded_size;
    visible_rect_ = NormalizeVisible(visible_rect, coded_size);
    timestamp_us_ = timestamp_us;
    duration_us_ = duration_us;
    plane_count_ = 0;
    texture_ = texture;
}

int VideoFrame::NumPlanes(VideoPixelFormat format) {
    switch (format) {
        case VideoPixelFormat::kBGRA8:
        case VideoPixelFormat::kRGBA8:
            return 1;
        case VideoPixelFormat::kNV12:
            return 2;
        case VideoPixelFormat::kI420:
            return 3;
    }
    return 0;
}

std::size_t VideoFrame::BytesRequired(VideoPixelFormat format, int w,
                                      int h) {
    Plane planes[kMaxPlanes]{};
    return ComputePlaneLayout(format, w, h, planes);
}

scoped_refptr<VideoFrame> VideoFrame::CreateOwned(
    VideoPixelFormat format, VideoSize coded_size,
    VideoRect visible_rect, std::int64_t timestamp_us,
    std::int64_t duration_us) {
    if (NumPlanes(format) == 0 || coded_size.IsEmpty()) {
        return nullptr;
    }
    const VideoRect vis = NormalizeVisible(visible_rect, coded_size);
    if (!vis.FitsInside(coded_size)) {
        return nullptr;
    }
    return scoped_refptr<VideoFrame>(new OwnedVideoFrame(
        format, coded_size, vis, timestamp_us, duration_us));
}

scoped_refptr<VideoFrame> VideoFrame::WrapOwnedMemory(
    VideoPixelFormat format, VideoSize coded_size,
    VideoRect visible_rect, std::int64_t timestamp_us,
    std::int64_t duration_us, std::vector<std::uint8_t>&& pixels) {
    if (NumPlanes(format) != 1 || coded_size.IsEmpty()) {
        return nullptr;
    }
    const VideoRect vis = NormalizeVisible(visible_rect, coded_size);
    if (!vis.FitsInside(coded_size)) {
        return nullptr;
    }
    if (pixels.size() !=
        BytesRequired(format, coded_size.width, coded_size.height)) {
        return nullptr;
    }
    return scoped_refptr<VideoFrame>(
        new OwnedVideoFrame(format, coded_size, vis, timestamp_us,
                            duration_us, std::move(pixels)));
}

scoped_refptr<VideoFrame> VideoFrame::WrapTexture(
    VideoPixelFormat format, VideoSize coded_size,
    VideoRect visible_rect, std::int64_t timestamp_us,
    std::int64_t duration_us, VideoTexture texture,
    ReleaseCallback mailbox_release) {
    if (coded_size.IsEmpty() || texture.shared_handle == nullptr) {
        return nullptr;
    }
    const VideoRect vis = NormalizeVisible(visible_rect, coded_size);
    if (!vis.FitsInside(coded_size)) {
        return nullptr;
    }
    return scoped_refptr<VideoFrame>(new TextureVideoFrame(
        format, coded_size, vis, timestamp_us, duration_us, texture,
        std::move(mailbox_release)));
}

// ---------------------------------------------------------------------
// VideoFramePool
// ---------------------------------------------------------------------

// static
void VideoFramePool::ReturnStorage(
    detail::VideoFramePoolCore& core, std::uint64_t key,
    std::shared_ptr<std::vector<std::uint8_t>> storage) {
    std::lock_guard<std::mutex> lk(core.mutex);
    auto& dq = core.idle[key];
    if (dq.size() >= core.options.max_idle_per_key) {
        ++core.stats.overflow_frees;
        return;  // storage dies with the shared_ptr going out of scope
    }
    core.idle_bytes += storage->size();
    dq.push_back(std::move(storage));
    ++core.stats.returned;
}

VideoFramePool::VideoFramePool() : VideoFramePool(Options{}) {}

VideoFramePool::VideoFramePool(Options options)
    : core_(std::make_shared<detail::VideoFramePoolCore>(options)) {}

VideoFramePool::~VideoFramePool() = default;

scoped_refptr<VideoFrame> VideoFramePool::Acquire(
    VideoPixelFormat format, VideoSize coded, VideoRect visible_rect,
    std::int64_t timestamp_us, std::int64_t duration_us) {
    if (VideoFrame::NumPlanes(format) == 0 || coded.IsEmpty()) {
        return nullptr;
    }
    const VideoRect vis = NormalizeVisible(visible_rect, coded);
    if (!vis.FitsInside(coded)) {
        return nullptr;
    }
    const std::uint64_t key = detail::VideoFramePoolCore::MakeKey(
        format, coded.width, coded.height);

    std::shared_ptr<std::vector<std::uint8_t>> storage;
    {
        std::lock_guard<std::mutex> lk(core_->mutex);
        ++core_->stats.acquires;
        auto& dq = core_->idle[key];
        if (!dq.empty()) {
            storage = std::move(dq.front());
            dq.pop_front();
            core_->idle_bytes -= storage->size();
            ++core_->stats.pool_hits;
        } else {
            ++core_->stats.pool_misses;
        }
    }
    if (!storage) {
        storage = std::make_shared<std::vector<std::uint8_t>>(
            VideoFrame::BytesRequired(format, coded.width,
                                      coded.height));
    }
    return scoped_refptr<VideoFrame>(new PooledVideoFrame(
        format, coded, vis, timestamp_us, duration_us,
        std::move(storage), core_, key));
}

void VideoFramePool::Flush() {
    std::lock_guard<std::mutex> lk(core_->mutex);
    core_->idle.clear();
    core_->idle_bytes = 0;
}

VideoFramePool::Stats VideoFramePool::stats() const {
    std::lock_guard<std::mutex> lk(core_->mutex);
    Stats s = core_->stats;
    s.idle_buffers = core_->CountIdle();
    s.idle_bytes = core_->idle_bytes;
    return s;
}

}  // namespace XER::VideoEditor::Render
