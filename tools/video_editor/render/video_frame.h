// render/video_frame.h — Chromium media::VideoFrame analogue for the
// VideoEditor render port (slice 1).
//
// See docs/wiki/architecture/chromium-render-port-plan.md §1/§3. This
// is the zero-copy hand-off primitive that replaces the current
// copy-chain (PreviewEngine::RenderAndDeliver memcpy ->
// PreviewBinding pending_frame_.assign -> MonitorSurface::SetFrame
// make_shared<vector> — copies A/B/C in plan §2). A frame is
// refcounted, carries immutable metadata once published, and has a
// pluggable backing: owned CPU planes today, an opaque texture handle
// slot for the D3D11VA/SharedImage work in tickets R3/R4.
//
// Chromium mapping (media/base/video_frame.h):
//   * StorageType::OWNED_MEMORY   -> VideoFrameStorage::kOwnedMemory
//   * StorageType::OPAQUE         -> VideoFrameStorage::kOpaqueTexture
//   * WrapSharedImage(..., ReleaseMailboxCB) -> WrapTexture(...) whose
//     release callback runs at last-Release — our single-process
//     mailbox-release (plan §3: SharedImage/Mailbox/SyncToken collapses
//     to a shared D3D11 handle + IDXGIKeyedMutex ordering).
//   * media::VideoFramePool       -> VideoFramePool (bounded, keyed by
//     format+coded_size; dying frames return their storage for reuse).
//
// UI-free by design: no ui/ includes, no Windows headers — it compiles
// into the exe today and can move into the UI-free core lib unchanged
// when slice 2 wires the compositor to it.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace XER::VideoEditor::Render {

// Minimal scoped_refptr — same contract as Chromium's
// base/memory/scoped_refptr.h: intrusive AddRef()/Release(), null-safe,
// releases on destruction. Defined locally because the tree has no
// shared refptr helper (video::Frame is by-value today).
template <typename T>
class scoped_refptr {
public:
    scoped_refptr() = default;
    scoped_refptr(std::nullptr_t) {}
    explicit scoped_refptr(T* p) : ptr_(p) {
        if (ptr_ != nullptr) ptr_->AddRef();
    }
    scoped_refptr(const scoped_refptr& r) : ptr_(r.ptr_) {
        if (ptr_ != nullptr) ptr_->AddRef();
    }
    template <typename U>
    scoped_refptr(const scoped_refptr<U>& r) : ptr_(r.get()) {
        if (ptr_ != nullptr) ptr_->AddRef();
    }
    scoped_refptr(scoped_refptr&& r) noexcept : ptr_(r.ptr_) {
        r.ptr_ = nullptr;
    }
    template <typename U>
    scoped_refptr(scoped_refptr<U>&& r) noexcept : ptr_(r.release()) {}
    ~scoped_refptr() {
        if (ptr_ != nullptr) ptr_->Release();
    }

    scoped_refptr& operator=(const scoped_refptr& r) {
        reset(r.ptr_);
        return *this;
    }
    scoped_refptr& operator=(scoped_refptr&& r) noexcept {
        if (ptr_ != nullptr) ptr_->Release();
        ptr_ = r.ptr_;
        r.ptr_ = nullptr;
        return *this;
    }

    T* get() const { return ptr_; }
    T* release() {
        T* p = ptr_;
        ptr_ = nullptr;
        return p;
    }
    void reset(T* p = nullptr) {
        if (p != nullptr) p->AddRef();
        if (ptr_ != nullptr) ptr_->Release();
        ptr_ = p;
    }
    T& operator*() const { return *ptr_; }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }
    void swap(scoped_refptr& r) noexcept { std::swap(ptr_, r.ptr_); }

    friend bool operator==(const scoped_refptr& a, const T* b) {
        return a.ptr_ == b;
    }
    friend bool operator!=(const scoped_refptr& a, const T* b) {
        return a.ptr_ != b;
    }

private:
    T* ptr_ = nullptr;
};

enum class VideoPixelFormat : std::uint8_t {
    kBGRA8 = 0,  // packed B,G,R,A — the canvas/upload format today
    kRGBA8,      // packed R,G,B,A (FFmpeg RGBA sources)
    kNV12,       // bi-planar 4:2:0 — hwaccel output before conversion
    kI420,       // tri-planar 4:2:0
};

enum class VideoFrameStorage : std::uint8_t {
    kOwnedMemory = 0,  // CPU-mappable planes (StorageType::OWNED_MEMORY)
    kOpaqueTexture,    // GPU texture; unreachable from CPU (OPAQUE)
};

// Integer media-space geometry — gfx::Size/gfx::Rect analogues. The UI
// layout types are floats and live a layer up; frames must not depend
// on them.
struct VideoSize {
    int width = 0;
    int height = 0;
    bool IsEmpty() const { return width <= 0 || height <= 0; }
    friend bool operator==(const VideoSize& a, const VideoSize& b) {
        return a.width == b.width && a.height == b.height;
    }
    friend bool operator!=(const VideoSize& a, const VideoSize& b) {
        return !(a == b);
    }
};

struct VideoRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int Right() const { return x + width; }
    int Bottom() const { return y + height; }
    bool IsEmpty() const { return width <= 0 || height <= 0; }
    // True when the rect stays inside `size`'s bounds (origin 0,0).
    bool FitsInside(const VideoSize& size) const {
        return x >= 0 && y >= 0 && Right() <= size.width &&
               Bottom() <= size.height;
    }
    friend bool operator==(const VideoRect& a, const VideoRect& b) {
        return a.x == b.x && a.y == b.y && a.width == b.width &&
               a.height == b.height;
    }
    friend bool operator!=(const VideoRect& a, const VideoRect& b) {
        return !(a == b);
    }
};

// Opaque texture descriptor — the chromium ClientSharedImage+Mailbox
// pair collapsed to what a single process needs (plan §3): on Windows
// this is a D3D11 shared HANDLE (IDXGIResource::GetSharedHandle) plus
// the texture-array slice; ordering is the keyed mutex the existing
// D2DPresenter already uses, NOT a SyncToken. `generation` bumps when
// the producer recreates the resource (same role as
// Direct2DEngine::canvas_generation_ / D2DPresenter::Frame::generation).
struct VideoTexture {
    void* shared_handle = nullptr;  // HANDLE on Windows; EGLImage later
    std::uint32_t subresource = 0;  // D3D11 texture-array slice index
    std::uint64_t generation = 0;   // producer resource generation
};

// Internal pool-core handle — defined in video_frame.cc; named here so
// pooled frames can hold a weak_ptr back to it. Not API surface.
namespace detail {
struct VideoFramePoolCore;
}

// Refcounted, metadata-immutable video frame. Thread-safe refcount;
// plane pointers are stable for the frame's lifetime. Producers write
// pixels through data() BEFORE publishing the ref — treat frames as
// immutable once handed to a consumer (Chromium semantics; HasOneRef()
// is the exclusive-access check for safe in-place reuse).
class VideoFrame {
public:
    static constexpr int kMaxPlanes = 3;
    // Chromium's ReleaseMailboxCB: fired when the frame's last ref
    // releases — the producer's signal that the texture/buffer is
    // consumable-again (return to decoder pool, release keyed mutex,
    // retire the mailbox).
    using ReleaseCallback = std::function<void()>;

    // Internal layout description (public so file-local helpers and
    // future platform shims can compute plane geometry — not API).
    struct Plane {
        std::uint8_t* ptr = nullptr;
        int stride = 0;
        std::size_t size = 0;
    };

    // Allocates fresh CPU storage for `coded_size` in `format`.
    // `visible_rect` is the display-relevant sub-rect (empty means the
    // full coded size). Timestamps in microseconds — same unit
    // video::Frame::pts uses today.
    static scoped_refptr<VideoFrame> CreateOwned(
        VideoPixelFormat format, VideoSize coded_size,
        VideoRect visible_rect, std::int64_t timestamp_us,
        std::int64_t duration_us);
    // Adopts a caller-owned packed buffer for single-plane formats
    // (kBGRA8/kRGBA8; stride must be width*4). Zero-copy migration path
    // from the compositor's std::vector BGRA output — the
    // video::Frame::Adopt() analogue.
    static scoped_refptr<VideoFrame> WrapOwnedMemory(
        VideoPixelFormat format, VideoSize coded_size,
        VideoRect visible_rect, std::int64_t timestamp_us,
        std::int64_t duration_us, std::vector<std::uint8_t>&& pixels);
    // Wraps a GPU texture as a frame (WrapSharedImage analogue).
    // `mailbox_release` runs when the last ref dies.
    static scoped_refptr<VideoFrame> WrapTexture(
        VideoPixelFormat format, VideoSize coded_size,
        VideoRect visible_rect, std::int64_t timestamp_us,
        std::int64_t duration_us, VideoTexture texture,
        ReleaseCallback mailbox_release);

    void AddRef() const {
        refs_.fetch_add(1, std::memory_order_relaxed);
    }
    void Release() const {
        if (refs_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            delete this;
        }
    }
    bool HasOneRef() const {
        return refs_.load(std::memory_order_acquire) == 1;
    }

    VideoPixelFormat format() const { return format_; }
    VideoFrameStorage storage_type() const { return storage_; }
    VideoSize coded_size() const { return coded_size_; }
    VideoRect visible_rect() const { return visible_rect_; }
    std::int64_t timestamp_us() const { return timestamp_us_; }
    std::int64_t duration_us() const { return duration_us_; }

    // CPU-plane access — valid only when IsMappable().
    bool IsMappable() const {
        return storage_ == VideoFrameStorage::kOwnedMemory;
    }
    int planes() const { return plane_count_; }
    int stride(int plane) const;
    const std::uint8_t* data(int plane) const;
    // Writable view for the producer. Only safe while HasOneRef() —
    // publishing a ref makes the contents logically immutable.
    std::uint8_t* data(int plane);
    std::size_t plane_size(int plane) const;

    // kOpaqueTexture frames only; zero struct otherwise.
    const VideoTexture& texture() const { return texture_; }

    // Shares the packed CPU storage as a vector — the bridge onto the
    // legacy shared_ptr<const std::vector<std::uint8_t>> wire format
    // (MonitorSurface::Frame / ScopeFrame / the eyedropper seam).
    // The returned shared_ptr aliases the frame's real storage with
    // ZERO copies; pooled frames recycle only when they are the sole
    // remaining owner, so an outstanding alias can never observe a
    // buffer the pool already handed out again. Null for texture
    // frames and multi-planar formats.
    virtual std::shared_ptr<const std::vector<std::uint8_t>>
    SharePixels() const = 0;

    // Plane count / packed size helpers (unit-coverable math).
    static int NumPlanes(VideoPixelFormat format);
    // Total bytes the format needs for a coded w*h (all planes).
    static std::size_t BytesRequired(VideoPixelFormat format, int w,
                                     int h);

protected:
    VideoFrame() = default;
    virtual ~VideoFrame() = default;
    void InitCpu(VideoPixelFormat format, VideoSize coded_size,
                 VideoRect visible_rect, std::int64_t timestamp_us,
                 std::int64_t duration_us, const Plane* planes,
                 int plane_count);
    void InitTexture(VideoPixelFormat format, VideoSize coded_size,
                     VideoRect visible_rect, std::int64_t timestamp_us,
                     std::int64_t duration_us, VideoTexture texture);

private:
    VideoPixelFormat format_ = VideoPixelFormat::kBGRA8;
    VideoFrameStorage storage_ = VideoFrameStorage::kOwnedMemory;
    VideoSize coded_size_{};
    VideoRect visible_rect_{};
    std::int64_t timestamp_us_ = 0;
    std::int64_t duration_us_ = 0;
    Plane planes_[kMaxPlanes]{};
    int plane_count_ = 0;
    VideoTexture texture_{};
    mutable std::atomic<std::uint32_t> refs_{0};
};

// Bounded frame pool keyed by (format, coded_size) — the media::
// VideoFramePool analogue and the primitive that makes the zero-copy
// hand-off real: the compositor Acquire()s, fills the planes, publishes
// the ref; when the last consumer Releases, the storage returns to the
// pool instead of hitting the allocator. A 1080p BGRA frame is
// ~8.3 MB — recycling turns the per-frame malloc+free into a deque pop.
//
// Thread safety: Acquire() may be called from any thread (the pool
// serializes internally); frame destruction recycles from whatever
// thread drops the last ref. Destroying the pool while frames are out
// is safe — orphaned storages just free on death.
class VideoFramePool {
public:
    struct Options {
        // Idle storages kept per (format,size) key. 4 covers triple
        // buffering + one in-flight marshal with headroom.
        std::size_t max_idle_per_key = 4;
    };

    VideoFramePool();
    explicit VideoFramePool(Options options);
    ~VideoFramePool();
    VideoFramePool(const VideoFramePool&) = delete;
    VideoFramePool& operator=(const VideoFramePool&) = delete;

    // Returns a CPU-storage frame for `coded` in `format`. Reuses a
    // recycled storage when one is idle for the key; allocates fresh
    // otherwise. Null on invalid arguments (bad format/size/rect).
    scoped_refptr<VideoFrame> Acquire(VideoPixelFormat format,
                                    VideoSize coded,
                                    VideoRect visible_rect,
                                    std::int64_t timestamp_us,
                                    std::int64_t duration_us);
    // Drops every idle storage (in-flight frames recycle into an empty
    // pool and free instead — memory pressure relief).
    void Flush();

    struct Stats {
        std::uint64_t acquires = 0;
        std::uint64_t pool_hits = 0;      // recycled storages handed out
        std::uint64_t pool_misses = 0;    // fresh allocations
        std::uint64_t returned = 0;       // storages recycled on death
        std::uint64_t overflow_frees = 0; // died past the idle cap
        // Died while a SharePixels() alias was still outstanding —
        // NOT recycled (an aliased buffer could be re-acquired and
        // overwritten under the reader); frees with the alias.
        std::uint64_t aliased_frees = 0;
        std::size_t idle_buffers = 0;     // storages waiting in the pool
        std::size_t idle_bytes = 0;
    };
    Stats stats() const;

    // Internal recycling entry point for pooled frame destructors —
    // not API surface (kept public so the frame subclass in
    // video_frame.cc can reach it without friend gymnastics).
    static void ReturnStorage(
        detail::VideoFramePoolCore& core, std::uint64_t key,
        std::shared_ptr<std::vector<std::uint8_t>> storage);

private:
    std::shared_ptr<detail::VideoFramePoolCore> core_;
};

}  // namespace XER::VideoEditor::Render
