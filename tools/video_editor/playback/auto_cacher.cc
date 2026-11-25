// auto_cacher.cc — see auto_cacher.h for the contract. One
// low-priority worker thread owns a private Compositor (its own
// decoder sessions) and walks a frame-indexed window around the
// playhead in increasing order; every RenderFrameAt fills the SHARED
// EffectFrameCache the playback compositor reads, which is the whole
// point — playback ticks then hit instead of decode+chain recompute.
//
// Frame times live as integer frame indices n -> t = n * frame_dt:
// dedup is exact, the queue is a plain increasing deque, and the
// engine's frame-snapped render time makes a playback request for
// the same frame an identical cache key (see preview_engine.cc's
// RenderAndDeliver).
#include "auto_cacher.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>   // SetThreadPriority — worker stays below playback
#endif

namespace XER::VideoEditor {

namespace {

bool Fail(std::string* error_out, const std::string& message) {
    if (error_out != nullptr) {
        *error_out = message;
    }
    return false;
}

}  // namespace

AutoCacher::AutoCacher() = default;
AutoCacher::~AutoCacher() { Shutdown(); }

bool AutoCacher::EnabledByEnv() {
    const char* env = std::getenv("XER_VE_AUTOCACHE");
    return !(env != nullptr && std::strcmp(env, "0") == 0);
}

std::int64_t AutoCacher::FrameIndexAt(double t, double frame_dt) {
    if (!std::isfinite(t) || t <= 0.0 || frame_dt <= 0.0) {
        return 0;
    }
    // floor matches the engine's render-time snap — the frame the
    // playhead is INSIDE, not the nearest one.
    return static_cast<std::int64_t>(std::floor(t / frame_dt + 1e-9));
}

bool AutoCacher::Init(const Project& snapshot, const Options& options,
                      std::string* error_out) {
    // Validate first — after Shutdown() there is no failure path, so
    // initializing_ is always released before this returns.
    if (snapshot.width <= 0 || snapshot.height <= 0) {
        return Fail(error_out, "invalid canvas size");
    }
    if (snapshot.fps_num <= 0 || snapshot.fps_den <= 0) {
        return Fail(error_out, "invalid project fps");
    }
    if (!options.frame_cache) {
        return Fail(error_out, "autocache requires a frame_cache");
    }
    {
        // Arm BEFORE Shutdown: stop_ flickers true while the old
        // worker joins, and WaitIdle callers (smoke checks, HUDs)
        // must not read that transient as a settled worker.
        std::lock_guard<std::mutex> lk(mutex_);
        initializing_ = true;
    }
    Shutdown();   // joins any previous worker; safe re-init
    project_ = snapshot;
    options_ = options;
    factory_ = options_.decoder_factory
                   ? options_.decoder_factory
                   : DefaultDecoderFactory();
    cache_ = options_.frame_cache;
    duration_ = project_.duration();
    frame_dt_ = static_cast<double>(project_.fps_den) /
                static_cast<double>(project_.fps_num);
    if (duration_ > 0.0) {
        // Compositor::FrameCount formula — last renderable index.
        const auto count = static_cast<std::int64_t>(std::llround(
            duration_ * static_cast<double>(project_.fps_num) /
            static_cast<double>(project_.fps_den)));
        last_frame_ = std::max<std::int64_t>(0, count - 1);
    } else {
        last_frame_ = -1;
    }
    const double ahead_s = std::max(0.0, options_.ahead_seconds);
    const double behind_s = std::max(0.0, options_.behind_seconds);
    ahead_frames_ = static_cast<std::int64_t>(
        std::llround(ahead_s / frame_dt_));
    behind_frames_ = static_cast<std::int64_t>(
        std::llround(behind_s / frame_dt_));
    rewindow_frames_ = std::max<std::int64_t>(1, ahead_frames_ / 2);
    // Budget: a window bigger than the cache evicts its own head —
    // cap frames at the LRU-share fraction of capacity over a canvas
    // frame's byte size (clip-space frames are usually <= canvas).
    const double frame_bytes =
        static_cast<double>(project_.width) *
        static_cast<double>(project_.height) * 4.0;
    const double frac = std::isfinite(options_.cache_budget_fraction)
                            ? std::clamp(options_.cache_budget_fraction,
                                         0.0, 1.0)
                            : 0.5;
    const std::size_t by_budget =
        frame_bytes > 0.0
            ? static_cast<std::size_t>(
                  static_cast<double>(cache_->CapacityBytes()) * frac /
                  frame_bytes)
            : options_.max_frames;
    budget_frames_ =
        std::max<std::size_t>(1, std::min(options_.max_frames, by_budget));

    {
        std::lock_guard<std::mutex> lk(mutex_);
        stop_ = false;
        busy_ = false;
        dirty_ = true;   // prime the head of the sequence on startup
        anchor_ = -1;
        settle_at_ = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<
                         std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(
                             std::max(0.0, options_.settle_seconds)));
        queue_.clear();
        rendered_.clear();
        quality_ = (std::isfinite(options_.quality) &&
                    options_.quality > 0.0f && options_.quality < 1.0f)
                       ? options_.quality
                       : 1.0f;
        // stats_ deliberately NOT reset: the PreviewEngine re-Inits
        // this worker on every quality-tier flip (canvas dims are
        // cache-key material), and stats readers — smoke checks, perf
        // HUDs — rely on the counters staying monotonic across those
        // re-binds. Shutdown() clears everything anyway.
        warnings_.clear();
        warned_once_.clear();
    }
    playhead_.store(0.0, std::memory_order_relaxed);
    worker_ = std::thread(&AutoCacher::WorkerMain, this);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        initializing_ = false;
    }
    // Nudge WaitIdle waiters parked across the re-bind so they can
    // re-evaluate against the fresh (dirty) state.
    idle_cv_.notify_all();
    return true;
}

void AutoCacher::Shutdown() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        stop_ = true;
        stats_.cancelled += queue_.size();
        queue_.clear();
        comp_.Cancel();   // abort an in-flight render fast
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
    // Worker is joined — its render state is safe to touch here.
    comp_.Shutdown();
    comp_open_ = false;
    last_rendered_t_ = -1.0;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        busy_ = false;
    }
    idle_cv_.notify_all();
}

// ---- notifiers (any thread; UI-safe: lock + atomic + cv only) ------

void AutoCacher::OnSeek(double t) {
    if (!std::isfinite(t)) {
        return;
    }
    playhead_.store(t, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stop_) {
            return;
        }
        stats_.cancelled += queue_.size();
        queue_.clear();
        dirty_ = true;   // owe a fresh window once seeks settle
        settle_at_ = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<
                         std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(
                             std::max(0.0, options_.settle_seconds)));
        comp_.Cancel();
    }
    cv_.notify_all();
}

void AutoCacher::OnPlayhead(double t) {
    if (!std::isfinite(t)) {
        return;
    }
    playhead_.store(t, std::memory_order_relaxed);
    // Wake the worker only when the anchor has drifted a re-window's
    // worth AND a schedule could run right now — continuous playback
    // never issues OnSeek, so this is the only path that keeps the
    // runway ahead of a moving playhead once the queue drained. The
    // lock is uncontended at tick rates (the worker drops it around
    // every render).
    std::lock_guard<std::mutex> lk(mutex_);
    if (!stop_ && queue_.empty() && WindowDueLocked() &&
        std::chrono::steady_clock::now() >= settle_at_) {
        cv_.notify_all();
    }
}

void AutoCacher::OnQualityChanged(float scale) {
    const float q =
        (std::isfinite(scale) && scale > 0.0f && scale < 1.0f)
            ? scale
            : 1.0f;
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stop_) {
            return;
        }
        if (q == quality_) {
            return;   // same cache tier — keys unchanged
        }
        quality_ = q;
        stats_.cancelled += queue_.size();
        queue_.clear();
        rendered_.clear();   // quality is key material — all stale
        dirty_ = true;
        settle_at_ = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<
                         std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(
                             std::max(0.0, options_.settle_seconds)));
        comp_.Cancel();
    }
    cv_.notify_all();
}

void AutoCacher::Invalidate() {
    {
        std::lock_guard<std::mutex> lk(mutex_);
        if (stop_) {
            return;
        }
        stats_.cancelled += queue_.size();
        queue_.clear();
        rendered_.clear();
        dirty_ = true;
        settle_at_ = std::chrono::steady_clock::now() +
                     std::chrono::duration_cast<
                         std::chrono::steady_clock::duration>(
                         std::chrono::duration<double>(
                             std::max(0.0, options_.settle_seconds)));
        comp_.Cancel();
    }
    cv_.notify_all();
}

// ---- introspection ---------------------------------------------------

AutoCacher::Stats AutoCacher::GetStats() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return stats_;
}

std::size_t AutoCacher::PendingCount() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return queue_.size();
}

bool AutoCacher::WaitIdle(int timeout_ms) {
    std::unique_lock<std::mutex> lk(mutex_);
    return idle_cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms),
                             [this] {
                                 // stop_ reports a real teardown only —
                                 // during Init's re-bind it is a
                                 // transient behind initializing_.
                                 return !initializing_ &&
                                        (stop_ ||
                                         (!busy_ && queue_.empty() &&
                                          !WindowDueLocked()));
                             });
}

std::vector<std::string> AutoCacher::TakeWarnings() {
    std::lock_guard<std::mutex> lk(mutex_);
    std::vector<std::string> out;
    out.swap(warnings_);
    return out;
}

void AutoCacher::PushWarning(const std::string& message) {
    std::lock_guard<std::mutex> lk(mutex_);
    if (warned_once_.insert(message).second) {
        warnings_.push_back(message);
    }
}

// ---- worker ------------------------------------------------------------

bool AutoCacher::WindowDueLocked() const {
    if (last_frame_ < 0) {
        return false;   // nothing renderable in this snapshot
    }
    if (dirty_ || anchor_ < 0) {
        return true;
    }
    const std::int64_t ph =
        FrameIndexAt(playhead_.load(std::memory_order_relaxed),
                     frame_dt_);
    const std::int64_t drift = ph - anchor_;
    return drift > rewindow_frames_ || drift < -rewindow_frames_;
}

void AutoCacher::BuildWindowLocked() {
    const std::int64_t ph =
        FrameIndexAt(playhead_.load(std::memory_order_relaxed),
                     frame_dt_);
    // Forward funded first from the frame budget — the playback
    // runway matters; the small backfill shrinks when budget binds.
    const std::int64_t room =
        static_cast<std::int64_t>(budget_frames_);
    std::int64_t fwd = std::min<std::int64_t>(
        ahead_frames_, std::max<std::int64_t>(0, last_frame_ - ph));
    fwd = std::min(fwd, std::max<std::int64_t>(0, room - 1));
    std::int64_t bwd = std::min<std::int64_t>(behind_frames_, ph);
    bwd = std::min(bwd, std::max<std::int64_t>(0, room - 1 - fwd));
    const std::int64_t lo = ph - bwd;
    const std::int64_t hi = std::min(last_frame_, ph + fwd);
    ++stats_.windows;
    for (std::int64_t n = lo; n <= hi; ++n) {
        if (rendered_.count(n) != 0) {
            ++stats_.skipped;   // already pre-rendered this session
            continue;
        }
        queue_.push_back(n);
        ++stats_.queued;
    }
    anchor_ = ph;
    dirty_ = false;
}

AutoCacher::RenderResult AutoCacher::RenderOne(double t,
                                               float quality) {
    // Session continuity, same rule the PreviewEngine applies:
    // RenderFrameAt assumes non-decreasing t — a window starting
    // behind the watermark (or a Cancel flag set by a seek abort)
    // rebinds the snapshot so decoders re-seek honestly.
    if (comp_open_ &&
        (comp_.Cancelled() || t < last_rendered_t_ - 0.5 * frame_dt_)) {
        comp_.Shutdown();
        comp_open_ = false;
    }
    if (!comp_open_) {
        CompositorOptions opts;
        opts.decoder_factory = factory_;
        opts.frame_cache = cache_;
        opts.media_resolver = options_.media_resolver;
        std::string err;
        std::vector<std::string> warnings;
        if (!comp_.Init(project_, opts, &err, &warnings)) {
            PushWarning("autocache compositor init failed: " + err);
            return RenderResult::kFailed;
        }
        for (const std::string& w : warnings) {
            PushWarning("autocache: " + w);
        }
        comp_open_ = true;
        last_rendered_t_ = -1.0;
    }
    // Sticky per compositor, but the tier is cache-key material —
    // apply every render so a quality flip takes effect without
    // needing a session re-open.
    comp_.SetPreviewQuality(quality);
    std::string err;
    if (!comp_.RenderFrameAt(t, &scratch_, &err)) {
        if (comp_.Cancelled()) {
            return RenderResult::kCancelled;
        }
        PushWarning("autocache render failed at t=" +
                    std::to_string(t) + ": " + err);
        return RenderResult::kFailed;
    }
    last_rendered_t_ = t;
    return RenderResult::kDone;
}

void AutoCacher::WorkerMain() {
#ifdef _WIN32
    // Below the playback worker — pre-render must never starve the
    // realtime path. Best-effort: a failure just leaves normal prio.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
    std::unique_lock<std::mutex> lk(mutex_);
    for (;;) {
        // Wait for queued work or for a window to come due. The
        // settle deadline uses wait_until so a seek storm coalesces:
        // every new seek pushes settle_at_ forward and re-arms this.
        while (!stop_ && queue_.empty()) {
            if (!WindowDueLocked()) {
                idle_cv_.notify_all();
                cv_.wait(lk);
                continue;
            }
            if (std::chrono::steady_clock::now() < settle_at_) {
                cv_.wait_until(lk, settle_at_);
                continue;
            }
            BuildWindowLocked();   // may leave queue empty (all dedup'd)
        }
        if (stop_) {
            break;
        }
        const std::int64_t n = queue_.front();
        queue_.pop_front();
        const double t = static_cast<double>(n) * frame_dt_;
        // Stale-drop: the live playhead moved well past this entry
        // (continuous playback outran the queue — seeks purge instead
        // of relying on this). The margin keeps the pause backfill.
        const double ph = playhead_.load(std::memory_order_relaxed);
        if (t < ph - (options_.behind_seconds + 4.0 * frame_dt_)) {
            ++stats_.skipped;
            continue;
        }
        busy_ = true;
        const float quality = quality_;
        lk.unlock();
        const RenderResult result = RenderOne(t, quality);
        lk.lock();
        busy_ = false;
        switch (result) {
            case RenderResult::kDone:
                rendered_.insert(n);
                ++stats_.done;
                break;
            case RenderResult::kCancelled:
                ++stats_.cancelled;
                break;
            case RenderResult::kFailed:
                ++stats_.failed;
                break;
        }
        if (queue_.empty()) {
            idle_cv_.notify_all();
        }
        // Uncontended CPU is free; a contested one belongs to playback.
        lk.unlock();
        std::this_thread::yield();
        lk.lock();
    }
    idle_cv_.notify_all();
}

}  // namespace XER::VideoEditor
