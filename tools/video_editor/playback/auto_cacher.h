// Olive-style PreviewAutoCacher for the VideoEditor (see
// docs/wiki/research/2026-11-21-nle-architectures.md §4): a
// low-priority background worker that pre-renders a window of
// frame-aligned timeline times around the playhead so PLAYBACK
// renders become EffectFrameCache hits instead of decode+effect-chain
// recomputes.
//
// Concurrency seam: a Compositor is NOT thread-safe and assumes
// non-decreasing t per session, so the worker owns its OWN Compositor
// instance (its own decoder sessions — Olive's "private project copy"
// analog; the cost is a second decoder set on the same media). The
// shared EffectFrameCache is internally synchronized and explicitly
// documented as safe across compositors on different threads — that
// is the only shared mutable structure. Nothing here touches the
// preview engine's render path, so real playback is never blocked;
// the worker thread also drops to a lower OS priority where the
// platform allows.
//
// Scheduling model:
//   * OnSeek(t)      — discrete jump: the pending queue is purged and
//                      an in-flight render is cooperatively aborted
//                      (Compositor::Cancel); a new window is built only
//                      after `settle_seconds` of seek quiet (~400 ms),
//                      so a scrub storm never spawns decode work.
//   * OnPlayhead(t)  — cheap continuous anchor update (one atomic
//                      store — safe to call every engine tick). When
//                      the anchor drifts past half the forward window
//                      a fresh window is scheduled, so 1x playback
//                      keeps ~1.5-3 s of runway ahead.
//   * OnQualityChanged / Invalidate — cache-invalidating state change
//                      (preview quality is folded into the cache state
//                      hash): purges the queue AND the dedup set, then
//                      reschedules after the same settle.
//
// Window shape: [playhead - behind, playhead + ahead] snapped to
// project frame indices, rendered in strictly increasing order (one
// sequential forward decode pass per compositor session — backward
// jumps would serve stale and trip the honest-serve gate anyway).
// Forward gets priority: the forward allotment is funded first from
// the frame budget, the small backfill second (frames behind the
// playhead were usually just rendered and are already hot). Entries
// the live playhead has left behind are dropped at pop time
// (stale-drop), and entries the autocache itself already rendered
// this session are skipped (dedup set) — an already-cached frame is
// otherwise just a cheap Lookup+warp inside RenderClipLayer.
//
// Budget safety: the 512 MB LRU could be thrashed by a window larger
// than the cache, so the forward window is capped to
// `cache_budget_fraction` of the cache capacity divided by the
// canvas frame size (and by `max_frames`). Worst case is still safe —
// LRU eviction only ever costs recompute, never correctness.
//
// Toggle: XER_VE_AUTOCACHE=0 disables (EnabledByEnv); default ON.
// The engine additionally gates on PreviewOptions::autocache.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "compositor.h"      // Compositor + CompositorOptions
#include "media_decoder.h"   // MediaDecoderFactory
#include "timeline_model.h"  // Project

namespace XER::VideoEditor {

class AutoCacher {
public:
    struct Options {
        // Must match the render path's — empty resolves to
        // DefaultDecoderFactory() like the compositor itself.
        MediaDecoderFactory decoder_factory;
        // Shared cache the pre-renders fill; REQUIRED — a null cache
        // makes Init fail (pre-rendering with nowhere to store is a
        // wasted thread).
        std::shared_ptr<EffectFrameCache> frame_cache;
        // Seek-quiet delay before a window is (re)built — coalesces
        // scrub storms and burst state edits.
        double settle_seconds = 0.4;
        // Forward window — the playback runway. Capped by the frame
        // budget below before it ever reaches this many frames.
        double ahead_seconds = 3.0;
        // Small backfill behind the playhead — usually already hot.
        double behind_seconds = 0.5;
        // Hard bound on queued frames per window regardless of budget.
        std::size_t max_frames = 360;
        // Share of the LRU byte capacity the window may occupy
        // (capacity * fraction / canvas frame bytes -> frame cap).
        double cache_budget_fraction = 0.5;
        // Worker render-quality hint — MUST mirror the engine's
        // preview quality for keys to match; updated live via
        // OnQualityChanged.
        float quality = 1.0f;
        // Optional media-path resolver forwarded to this worker's
        // private compositor — the proxy seam (see
        // CompositorOptions::media_resolver). Must mirror the render
        // path's resolver or pre-renders decode at a different
        // resolution than playback expects.
        std::function<std::string(const std::string&)> media_resolver;
    };

    // Counters for the smoke log / HUD — all monotonic per Init.
    struct Stats {
        std::uint64_t windows = 0;     // windows scheduled
        std::uint64_t queued = 0;      // frame entries enqueued
        std::uint64_t done = 0;        // frames rendered (hit or store)
        std::uint64_t skipped = 0;     // dedup + stale drops
        std::uint64_t cancelled = 0;   // purged + in-flight aborts
        std::uint64_t failed = 0;      // RenderFrameAt errors
    };

    AutoCacher();
    ~AutoCacher();   // Shutdown()
    AutoCacher(const AutoCacher&) = delete;
    AutoCacher& operator=(const AutoCacher&) = delete;

    // Env toggle: XER_VE_AUTOCACHE=0 -> false. Any other value (or
    // unset) keeps the feature on.
    static bool EnabledByEnv();

    // Binds a project snapshot (copied — Premiere sequence semantics,
    // edits arrive as a re-Init) and starts the worker. Fails only on
    // invalid fps/canvas or a missing frame_cache. A re-Init is a full
    // invalidate: queue + dedup set cleared, worker re-primed.
    bool Init(const Project& snapshot, const Options& options,
              std::string* error_out = nullptr);
    void Shutdown();   // cancels + joins the worker; idempotent

    // Discrete playhead jump (user seek, loop landing): purges the
    // pending queue, aborts an in-flight render, restarts the settle
    // timer. Cheap — UI-thread safe.
    void OnSeek(double t);
    // Continuous playhead update — one atomic store, safe to call
    // every playback tick. Never purges work; stale entries are
    // dropped at pop time instead.
    void OnPlayhead(double t);
    // Preview-quality tier changed — folded into the cache state
    // hash, so every previously rendered frame now lives under a
    // different key: full invalidate + reschedule at the new tier.
    void OnQualityChanged(float scale);
    // Generic cache-invalidating state change — same handling as a
    // quality change (purge queue + dedup set, reschedule).
    void Invalidate();

    Stats GetStats() const;
    std::size_t PendingCount() const;   // queued frames (introspection)
    // True once the queue is drained, no render is in flight and no
    // window is pending — for tests/smoke (see the idle contract in
    // the .cc; a settled-but-not-yet-scheduled window counts as busy
    // so a post-seek call waits through the whole prime).
    bool WaitIdle(int timeout_ms);
    // Non-fatal worker notes (init warnings, render failures),
    // drained once — harvested by the engine like compositor warnings.
    std::vector<std::string> TakeWarnings();

private:
    enum class RenderResult { kDone, kCancelled, kFailed };

    void WorkerMain();
    void PushWarning(const std::string& message);
    // Locked-state helpers — see .cc.
    bool WindowDueLocked() const;
    void BuildWindowLocked();
    RenderResult RenderOne(double t, float quality);

    static std::int64_t FrameIndexAt(double t, double frame_dt);

    Project project_;
    Options options_;
    MediaDecoderFactory factory_;
    std::shared_ptr<EffectFrameCache> cache_;
    double duration_ = 0.0;
    double frame_dt_ = 1.0 / 30.0;
    // Precomputed window geometry (Init-time; in frame indices).
    std::int64_t last_frame_ = -1;      // <0: no renderable frames
    std::int64_t ahead_frames_ = 0;
    std::int64_t behind_frames_ = 0;
    std::int64_t rewindow_frames_ = 1;
    std::size_t budget_frames_ = 0;     // LRU-share cap

    // ---- scheduling state (mutex_-guarded) --------------------------
    mutable std::mutex mutex_;
    std::condition_variable cv_;        // work / settle wakeups
    std::condition_variable idle_cv_;   // WaitIdle
    bool stop_ = false;
    bool busy_ = false;                 // a render is in flight
    bool dirty_ = false;                // a window is owed (seek/edit)
    std::int64_t anchor_ = -1;          // frame idx the queue was built around
    std::chrono::steady_clock::time_point settle_at_;
    std::deque<std::int64_t> queue_;    // frame indices, increasing
    // Frame indices the autocache already rendered this session —
    // survives seeks (the cache entries are still valid), cleared on
    // quality change / Invalidate / re-Init.
    std::unordered_set<std::int64_t> rendered_;
    float quality_ = 1.0f;
    Stats stats_;
    std::vector<std::string> warnings_;
    std::set<std::string> warned_once_;

    // Cheap atomic the notifiers update without touching mutex_.
    std::atomic<double> playhead_{0.0};

    // ---- worker-owned render state (worker thread only) -------------
    Compositor comp_;
    bool comp_open_ = false;
    double last_rendered_t_ = -1.0;   // session-continuity watermark
    std::vector<std::uint8_t> scratch_;
    std::thread worker_;
};

}  // namespace XER::VideoEditor
