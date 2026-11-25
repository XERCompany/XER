// preview_engine.cc — see preview_engine.h for the contract. All
// pixel/audio work happens on the single worker thread; the mutex
// below guards ONLY the command block (UI -> worker), never the
// render path, so sink callbacks can never deadlock on engine state.
#include "preview_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "l2_frame_cache.h"   // disk tier behind frame_cache_
#include "render/frame_scheduler.h"   // chromium render-port slice 2
#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"   // AudioFrameInfo

namespace XER::VideoEditor {
namespace {
namespace FFmpeg = XER::Bridge::FFmpeg;
namespace RenderNs = XER::VideoEditor::Render;

// Same-direction tolerance for the session-continuity check — below
// this a tick is "not moving backwards" (sub-nanosecond clock noise).
constexpr double kTimeEpsilon = 1e-6;
// Upper bound of mixer chunks drained per worker tick — bounds
// worst-case per-frame audio work; leftovers continue next tick.
constexpr int kMaxAudioChunksPerTick = 64;
// Forward chunk-index gap beyond which the mixer session re-opens at
// the target instead of draining every crossed chunk (large forward
// seeks would otherwise mix seconds of audio nobody hears).
constexpr std::size_t kAudioGapSkipChunks = 96;   // ~2 s at 1024/48k
// Audio runs AHEAD of the playhead by this much (free-running stream
// paced by the device buffer): the ~200 ms WASAPI cushion means a slow
// render tick never starves the output. Re-anchoring happens only on
// real discontinuities (user seek, backward, >2 s gap) — never on
// ordinary per-tick jitter.
constexpr double kAudioLeadSeconds = 0.12;
// Audio-master drift correction (chromium render-port R8): while the
// sink is actively consuming samples the AudioClockTimeSource is the
// authoritative media clock — the wall estimate is rebased onto the
// audible position (written frontier minus the structural lead) only
// when the divergence exceeds this threshold. ~3 frames at 30 fps;
// absorbs the unknown sink-buffer residual without per-tick jitter.
constexpr double kAudioDriftCorrectSeconds = 0.10;

// Measured adaptive-quality ladder (PreviewOptions::adaptive_quality):
// the user's playback pick is multiplied by these step factors —
// level 0 = full pick, 1 = half, 2 = quarter, 3 = eighth. ~500 ms
// hysteresis windows gate each step so the tier can't flap, and a
// pause always restores level 0 (paused frames render full-res
// anyway — Premiere's separate Paused Resolution).
constexpr float kAdaptiveSteps[] = {1.0f, 0.5f, 0.25f, 0.125f};
constexpr int kAdaptiveMaxLevel = 3;
constexpr std::chrono::milliseconds kAdaptiveWindow{500};
// Window thresholds: a rolling render-time average sustained above
// 90% of the frame budget steps the ladder down; below 40% steps it
// back up (doc T4 — measured, not a menu).
constexpr double kAdaptiveHigh = 0.9;
constexpr double kAdaptiveLow = 0.4;
// EMA weight of each rendered frame in the rolling average.
constexpr double kAdaptiveEmaAlpha = 0.25;

// Quality-tier canvas scaling: the "1/2 / 1/4" monitor options shrink
// the canvas the compositor renders to, so warp, blend, layer and
// canvas copies all drop quadratically with scale — the honest form
// of preview-quality (a post-pass downsample would still pay full
// price for every upstream stage). Canvas dims are folded into the
// effect-cache state hash, so the scaled project is ALSO what the
// AutoCacher binds: keys stay aligned between pre-render and playback.
Project ScaledRenderProject(const Project& p, double scale) {
    if (!(scale > 0.0) || scale >= 1.0) {
        return p;
    }
    Project rp = p;
    rp.width = std::max(
        1, static_cast<int>(std::lround(p.width * scale)));
    rp.height = std::max(
        1, static_cast<int>(std::lround(p.height * scale)));
    return rp;
}

// The shared EffectFrameCache only stores clips with a non-empty
// effect or mask chain (see the `cacheable` gate in
// compositor.cc's RenderClipLayer — an empty chain is a memcpy, not
// worth a cache round-trip). A project without ANY such clip can
// never benefit from pre-rendering: the AutoCacher's second decoder
// set and render loop would burn a CPU core for zero hits.
bool ProjectHasCacheableContent(const Project& p) {
    for (const Clip& clip : p.clips) {
        if (!clip.effects.empty() || !clip.masks.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace

// ------------------------------------------------------------------
// PreviewEngine::Impl — all state lives here so the header stays slim.
// ------------------------------------------------------------------
struct PreviewEngine::Impl {
    // ---- configuration (written at Init, immutable afterwards) ----
    Project project_;
    PreviewOptions options_;
    PreviewEngine::Sink* sink_ = nullptr;
    MediaDecoderFactory factory_;       // resolved, never empty
    std::shared_ptr<FrameClock> clock_; // resolved, never null
    double duration_ = 0.0;
    double frame_dt_ = 1.0 / 30.0;      // project frame interval, s

    // ---- command state (UI -> worker); guarded by mutex_ ----
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool running_ = false;          // Init done, worker up
    bool stop_ = false;             // Shutdown requested
    bool playing_cmd_ = false;
    double rate_cmd_ = 1.0;
    float quality_cmd_ = 1.0f;
    double loop_in_cmd_ = 0.0;
    double loop_out_cmd_ = 0.0;     // loop_out <= loop_in -> disabled
    float master_gain_cmd_ = 0.0f;  // master fader dB -> mixer
    bool seek_pending_ = false;
    double seek_t_ = 0.0;
    bool present_pending_ = false;  // render one frame even when paused
    std::vector<std::string> warnings_;
    std::set<std::string> warned_once_;

    // ---- worker-owned playback state (worker thread only) ----
    Compositor comp_;
    // The canvas-scaled snapshot the compositor and the AutoCacher
    // bind — rebuilt when the applied quality tier flips (the
    // command block's quality_cmd_ is the request, render_scale_ the
    // tier actually bound). At scale 1 this is just project_.
    Project render_project_;
    float render_scale_ = 1.0f;
    // The EFFECTIVE playback tier (user pick x adaptive step) the
    // AutoCacher is bound at — tracked separately from render_scale_
    // so the pause<->play canvas flips (paused = full res) never
    // re-prime it; only real playback-tier changes do.
    float autocache_scale_ = 1.0f;
    // Set by the worker when autocache_scale_ changes — canvas dims
    // are cache-key material, so the AutoCacher must re-Init on the
    // scaled snapshot or it fills keys playback never requests.
    bool autocache_reset_pending_ = false;
    // Project-frame index (floor(t/frame_dt)) of the last successful
    // render — the basis of the dropped-frame count. -1 after any
    // discontinuity (seek, session reopen, Init).
    std::int64_t last_frame_idx_ = -1;
    // Measured adaptive ladder (worker-owned; adaptive_level_out_ is
    // the atomic mirror the UI reads for the badge).
    double ema_render_ms_ = -1.0;
    int adaptive_level_ = 0;
    std::chrono::steady_clock::time_point adaptive_window0_{};
    // Shared effect-chain result cache — injected into every
    // compositor session this engine opens. Survives ReopenCompositor
    // (every backward seek / loop wrap re-binds the compositor), so
    // scrubbing back over already-rendered frames hits the cache
    // instead of re-decoding + re-running stacked effects. Entries
    // carry a full input hash, so surviving project edits is also
    // safe — changed inputs simply miss.
    std::shared_ptr<EffectFrameCache> frame_cache_ =
        std::make_shared<EffectFrameCache>();
    // Disk tier behind frame_cache_ (l2_frame_cache.h — .dxcfz
    // spill). Created once at first Init when XER_VE_L2 allows and
    // attached to the shared cache — the AutoCacher's private
    // compositor hits/spills through the same tier automatically.
    std::shared_ptr<L2FrameCache> l2_cache_;
    // Olive-style idle-time pre-render into frame_cache_ — owns a
    // SECOND compositor (the only safe seam: Compositor is single-
    // consumer-thread), fills the shared cache on its own worker so
    // the playback ticks below hit. Null when disabled.
    std::unique_ptr<AutoCacher> autocache_;
    AudioMixer mixer_;
    bool comp_open_ = false;
    bool mixer_open_ = false;
    bool audio_broken_ = false;         // GetChunk hard-failed; stop pump
    // Media clocks (chromium render-port slice 2): wall_ts_ carries
    // the playhead — same anchor+delta*rate math the raw anchor_t_/
    // anchor_wall_ pair ran, but as a render::TimeSource so the
    // FrameScheduler can consume it. Its NowFn rides the injected
    // FrameClock, so manual-clock tests stay deterministic. When an
    // audio sink exists, audio_ts_ counts written samples
    // (AudioClock analogue) and becomes the scheduler's master +
    // the drift-correction reference for wall_ts_.
    RenderNs::WallClockTimeSource wall_ts_;
    std::unique_ptr<RenderNs::AudioClockTimeSource> audio_ts_;
    // BeginFrame-style scheduler (FrameScheduler/FrameTick): ticked
    // once per worker iteration, acked per presented frame. Its
    // counters mirror into sched_* atomics for the badge/tests.
    std::unique_ptr<RenderNs::FrameScheduler> sched_;
    // Samples-written value seen on the previous drift check —
    // corrections are gated on the counter actually advancing.
    std::int64_t last_audio_samples_ = -1;
    double last_render_t_ = -1.0;       // <0: nothing rendered this session
    double last_render_tr_ = -1.0;      // last frame-snapped render t
    std::size_t audio_next_index_ = 0;  // next chunk index to produce
    std::size_t audio_chunk_count_ = 0;
    double chunk_dur_ = 0.0;            // chunk_samples / sample_rate
    // Pooled delivery frames — the R9 zero-copy marshal: the
    // compositor renders straight into an acquired frame's plane and
    // the ref flows worker->binding->monitor with no pixel copies;
    // the storage recycles when the last consumer's ref dies.
    RenderNs::VideoFramePool frame_pool_;

    // ---- UI-readable atomics ----
    std::atomic<double> playhead_{0.0};
    std::atomic<bool> playing_{false};
    std::atomic<std::uint64_t> frames_presented_{0};
    // T1 counters — the dropped-frame indicator data: project-frame
    // indices the wall-clock playhead skipped without a render while
    // playing (beyond the rate's expected step), plus the last
    // RenderFrameAt wall time in ms.
    std::atomic<std::uint64_t> frames_dropped_{0};
    std::atomic<double> last_render_ms_{0.0};
    // T4 badge mirrors: the ladder level currently applied and the
    // canvas tier actually bound (paused monitors bind 1.0).
    std::atomic<int> adaptive_level_out_{0};
    std::atomic<float> effective_scale_out_{1.0f};
    // FrameScheduler counter mirrors (worker writes, UI reads) +
    // rebase offsets so ResetDroppedFrames zeroes them alongside the
    // engine counter — Premiere's per-play indicator reset.
    std::atomic<std::uint64_t> sched_dropped_{0};
    std::atomic<std::uint64_t> sched_missed_{0};
    std::atomic<std::uint64_t> sched_dropped_base_{0};
    std::atomic<std::uint64_t> sched_missed_base_{0};
    // Per-stage wall-clock totals (PerfStats) — written on the worker
    // under mutex_, read on the UI thread by GetPerfStats().
    PreviewEngine::PerfStats perf_;

    std::thread worker_;

    static bool Fail(std::string* error_out, const std::string& msg) {
        if (error_out != nullptr) {
            *error_out = msg;
        }
        return false;
    }

    // Dedup'd warning sink — worker thread; Warnings() copies out
    // under the same mutex.
    void WarnOnce(const std::string& message) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (warned_once_.insert(message).second) {
            warnings_.push_back(message);
        }
    }

    void AppendWarnings(const std::vector<std::string>& more) {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& w : more) {
            if (warned_once_.insert(w).second) {
                warnings_.push_back(w);
            }
        }
    }

    void HarvestEngineWarnings() {
        if (comp_open_) {
            AppendWarnings(comp_.Warnings());
        }
        if (mixer_open_) {
            AppendWarnings(mixer_.Warnings());
        }
        if (autocache_) {
            AppendWarnings(autocache_->TakeWarnings());
        }
    }

    // ---- init / shutdown -------------------------------------------

    bool Init(const Project& snapshot, const PreviewOptions& options,
              PreviewEngine::Sink* sink, std::string* error_out) {
        Shutdown();   // safe re-init: joins any previous worker
        if (sink == nullptr) {
            return Fail(error_out, "preview sink is required");
        }
        if (snapshot.width <= 0 || snapshot.height <= 0) {
            return Fail(error_out, "invalid canvas size");
        }
        if (snapshot.fps_num <= 0 || snapshot.fps_den <= 0) {
            return Fail(error_out, "invalid project fps");
        }
        if (options.sample_rate <= 0 || options.audio_chunk_samples <= 0) {
            return Fail(error_out, "invalid audio options");
        }
        project_ = snapshot;
        options_ = options;
        sink_ = sink;
        factory_ = options_.decoder_factory
                       ? options_.decoder_factory
                       : DefaultDecoderFactory();
        clock_ = options_.clock ? options_.clock : MakeSystemFrameClock();
        duration_ = project_.duration();
        // fps_num/fps_den were validated above.
        frame_dt_ = static_cast<double>(project_.fps_den) /
                    static_cast<double>(project_.fps_num);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stop_ = false;
            playing_cmd_ = false;
            rate_cmd_ = 1.0;
            quality_cmd_ = 1.0f;
            loop_in_cmd_ = 0.0;
            loop_out_cmd_ = 0.0;
            master_gain_cmd_ = 0.0f;
            seek_pending_ = false;
            // Present the first frame at t=0 even while paused — the
            // monitor shows the sequence head like Premiere.
            present_pending_ = true;
            warnings_.clear();
            warned_once_.clear();
            running_ = true;
        }
        comp_open_ = false;
        mixer_open_ = false;
        audio_broken_ = false;
        // Media clocks + BeginFrame scheduler. The wall source's NowFn
        // rides the injected FrameClock — injected manual clocks keep
        // every playhead advance deterministic. The audio source
        // exists only when a real sink can consume samples (it is fed
        // per Write in PumpAudio); it is the scheduler's master clock
        // when present — audio owns the TimeSource upstream too.
        wall_ts_ = RenderNs::WallClockTimeSource(
            [this] { return clock_->NowSeconds(); });
        audio_ts_ =
            (options_.audio_enabled && options_.audio_sink != nullptr)
                ? std::make_unique<RenderNs::AudioClockTimeSource>(
                      options_.sample_rate)
                : nullptr;
        sched_ = std::make_unique<RenderNs::FrameScheduler>(
            audio_ts_ != nullptr
                ? static_cast<RenderNs::TimeSource*>(audio_ts_.get())
                : static_cast<RenderNs::TimeSource*>(&wall_ts_));
        sched_->SetMediaFrameDuration(frame_dt_);
        last_audio_samples_ = -1;
        last_render_t_ = -1.0;
        last_render_tr_ = -1.0;
        last_frame_idx_ = -1;
        ema_render_ms_ = -1.0;
        adaptive_level_ = 0;
        adaptive_window0_ = {};
        render_scale_ = 1.0f;
        render_project_ = project_;
        autocache_scale_ = 1.0f;
        autocache_reset_pending_ = false;
        {
            std::lock_guard<std::mutex> lk(mutex_);
            perf_ = PreviewEngine::PerfStats{};
        }
        audio_next_index_ = 0;
        audio_chunk_count_ = 0;
        chunk_dur_ = 0.0;
        playhead_.store(0.0, std::memory_order_relaxed);
        playing_.store(false, std::memory_order_relaxed);
        frames_presented_.store(0, std::memory_order_relaxed);
        frames_dropped_.store(0, std::memory_order_relaxed);
        last_render_ms_.store(0.0, std::memory_order_relaxed);
        adaptive_level_out_.store(0, std::memory_order_relaxed);
        effective_scale_out_.store(1.0f, std::memory_order_relaxed);
        sched_dropped_.store(0, std::memory_order_relaxed);
        sched_missed_.store(0, std::memory_order_relaxed);
        sched_dropped_base_.store(0, std::memory_order_relaxed);
        sched_missed_base_.store(0, std::memory_order_relaxed);
        // Disk tier behind frame_cache_ — attach once; it survives
        // compositor session reopens alongside the RAM tier, so a
        // warm .dxcfz store makes even first-pass playback cheap.
        // A create failure degrades to RAM-only (non-fatal).
        if (frame_cache_->DiskTier() == nullptr &&
            L2FrameCache::EnabledByEnv()) {
            std::string l2_err;
            l2_cache_ = L2FrameCache::Create(&l2_err);
            if (l2_cache_ != nullptr) {
                frame_cache_->SetDiskTier(l2_cache_);
            } else {
                WarnOnce("L2 frame cache unavailable: " + l2_err);
            }
        }
        // AutoCacher: pre-renders the playhead window into
        // frame_cache_ on its own low-priority thread + private
        // compositor. Same (scaled) snapshot + factory, same cache —
        // playback renders then hit instead of re-decoding. Init
        // failures are non-fatal (the feature degrades to plain
        // playback). Skipped entirely when no clip carries a
        // cacheable chain — the EffectFrameCache only stores
        // effect/mask-bearing frames, so pre-rendering an fx-free
        // timeline would just burn a second decoder set for nothing.
        if (options_.autocache && AutoCacher::EnabledByEnv() &&
            frame_cache_->CapacityBytes() > 0 &&
            ProjectHasCacheableContent(project_)) {
            auto ac = std::make_unique<AutoCacher>();
            std::string ac_err;
            if (ac->Init(render_project_, AutoCacheOptions(),
                         &ac_err)) {
                autocache_ = std::move(ac);
            } else {
                WarnOnce("autocache disabled: " + ac_err);
            }
        }
        worker_ = std::thread(&Impl::WorkerMain, this);
        return true;
    }

    void Shutdown() {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            stop_ = true;
            running_ = false;
        }
        cv_.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
        // Safe only AFTER the playback worker is joined — it reads
        // autocache_ per tick (OnPlayhead / TakeWarnings) without the
        // mutex, so the reset must outlive its last tick.
        autocache_.reset();
        if (comp_open_) {
            comp_.Shutdown();
            comp_open_ = false;
        }
        if (mixer_open_) {
            mixer_.Shutdown();
            mixer_open_ = false;
        }
        playing_.store(false, std::memory_order_relaxed);
        sink_ = nullptr;
        duration_ = 0.0;
    }

    // ---- UI-thread commands -------------------------------------------

    void Seek(double t) {
        if (!std::isfinite(t)) {
            return;
        }
        const double clamped = std::clamp(t, 0.0, duration_);
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!running_) {
                return;
            }
            seek_t_ = clamped;
            seek_pending_ = true;
            present_pending_ = true;   // show the landing frame paused
        }
        playhead_.store(clamped, std::memory_order_relaxed);
        if (autocache_) {
            // Discrete jump — purge the pending window and restart
            // the settle timer (scrub storms never spawn decode).
            autocache_->OnSeek(clamped);
        }
        cv_.notify_all();
    }

    void SetPlaying(bool on) {
        {
            std::lock_guard<std::mutex> lk(mutex_);
            if (!running_) {
                return;
            }
            playing_cmd_ = on;
        }
        playing_.store(on, std::memory_order_relaxed);
        cv_.notify_all();
    }

    void SetRate(double rate) {
        if (!std::isfinite(rate)) {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) {
            return;
        }
        rate_cmd_ = rate;
    }

    void SetLoop(double in_s, double out_s) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) {
            return;
        }
        loop_in_cmd_ = std::clamp(in_s, 0.0, duration_);
        loop_out_cmd_ = std::clamp(out_s, 0.0, duration_);
    }

    void SetQuality(float scale) {
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) {
            return;
        }
        quality_cmd_ = (std::isfinite(scale) && scale > 0.0f)
                           ? std::min(scale, 1.0f)
                           : 1.0f;
        present_pending_ = true;   // paused monitors refresh quality
        if (autocache_) {
            // Quality is cache-key material — the autocache must
            // re-prime at the new tier or it fills unreachable keys.
            autocache_->OnQualityChanged(quality_cmd_);
        }
        cv_.notify_all();
    }

    void SetMasterGainDb(float db) {
        if (!std::isfinite(db)) {
            return;
        }
        std::lock_guard<std::mutex> lk(mutex_);
        if (!running_) {
            return;
        }
        master_gain_cmd_ = db;
    }

    float MasterGainDb() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return master_gain_cmd_;
    }

    // ---- session lifecycle (worker thread) ----------------------------

    AutoCacher::Options AutoCacheOptions() const {
        AutoCacher::Options aco;
        aco.decoder_factory = factory_;
        aco.frame_cache = frame_cache_;
        aco.media_resolver = options_.media_resolver;
        // The heavy-effect detour tier must equal the playback
        // compositor's — preview_quality is folded into the cache
        // state hash, so a mismatched tier fills unreachable keys.
        // This is the PLAYBACK tier (pick x adaptive step): paused
        // full-res frames don't ride the shared cache.
        aco.quality = autocache_scale_;
        return aco;
    }

    void ReopenCompositor() {
        if (comp_open_) {
            comp_.Shutdown();
            comp_open_ = false;
        }
        CompositorOptions opts;
        opts.decoder_factory = factory_;
        opts.frame_cache = frame_cache_;
        opts.media_resolver = options_.media_resolver;
        std::string err;
        std::vector<std::string> w;
        if (!comp_.Init(render_project_, opts, &err, &w)) {
            // Pre-validated at Init — only reachable on allocation
            // failure. Warn once; the next discontinuity retries.
            WarnOnce("compositor init failed: " + err);
            return;
        }
        AppendWarnings(w);
        comp_open_ = true;
    }

    // (Re)binds the mixer so chunk production can start at the index
    // containing t. Lazy decode inside GetChunk seeks near the needed
    // media time, so mid-file reopens are cheap.
    void ReopenMixer(double t) {
        if (mixer_open_) {
            mixer_.Shutdown();
            mixer_open_ = false;
        }
        AudioMixerOptions opts;
        opts.decoder_factory = factory_;
        opts.sample_rate = options_.sample_rate;
        opts.chunk_samples = options_.audio_chunk_samples;
        std::string err;
        std::vector<std::string> w;
        if (!mixer_.Init(project_, opts, &err, &w)) {
            WarnOnce("audio mixer init failed: " + err);
            audio_broken_ = true;   // don't retry every tick
            return;
        }
        AppendWarnings(w);
        mixer_open_ = true;
        audio_broken_ = false;
        audio_chunk_count_ = mixer_.ChunkCount();
        chunk_dur_ = static_cast<double>(options_.audio_chunk_samples) /
                     static_cast<double>(options_.sample_rate);
        audio_next_index_ = ChunkIndexAt(t);
        if (audio_ts_ != nullptr) {
            // AudioClock SetBaseTimestamp: the written frontier starts
            // at this chunk's media position and the sample
            // accumulator re-zeroes against it.
            audio_ts_->Start(
                static_cast<double>(audio_next_index_) * chunk_dur_);
            last_audio_samples_ = -1;
        }
    }

    std::size_t ChunkIndexAt(double t) const {
        if (audio_chunk_count_ == 0) {
            return 0;
        }
        if (t >= duration_ || chunk_dur_ <= 0.0) {
            return audio_chunk_count_ - 1;
        }
        const double idx = std::clamp(
            t / chunk_dur_, 0.0,
            static_cast<double>(audio_chunk_count_ - 1));
        return static_cast<std::size_t>(idx);
    }

    // ---- render / pump (worker thread) --------------------------------

    void RenderAndDeliver(double t, bool playing, double rate) {
        if (!comp_open_) {
            return;
        }
        // Heavy-effect quality hint: <1.0 runs the expensive spatial
        // kernels on a downscaled frame inside the compositor. This
        // rides on top of the canvas scaling (render_project_) — the
        // tier matches what the AutoCacher renders at so the shared
        // cache keys line up. Export/screenshot compositors never set
        // either — full quality stays bit-exact.
        comp_.SetPreviewQuality(render_scale_);
        // Frame-snapped render time: the cache key quantizes the
        // request to microseconds, so a raw wall-clock t would make
        // every tick a distinct key — nothing could ever hit, and the
        // AutoCacher's frame-boundary pre-renders would be useless.
        // Snapping DOWN to the containing project frame gives every
        // request inside the frame interval one identical key (the
        // frame an NLE would display at t anyway); the reported
        // playhead and audio clock stay on real t.
        const double tr = std::clamp(
            std::floor(t / frame_dt_ + 1e-9) * frame_dt_, 0.0,
            duration_);
        // Dropped-frame counting (Premiere's stoplight data): the
        // playhead index vs the last rendered index. Only skips
        // beyond the rate's expected step count — a 2x shuttle
        // legitimately renders every other frame — and only while
        // playing (paused presents aren't drops). Discontinuities
        // reset last_frame_idx_ (seeks/reopens), so a landing never
        // counts as a drop.
        const std::int64_t frame_idx =
            static_cast<std::int64_t>(std::llround(tr / frame_dt_));
        if (playing && last_frame_idx_ >= 0) {
            const std::int64_t step = std::max<std::int64_t>(
                1, static_cast<std::int64_t>(
                       std::llround(std::fabs(rate))));
            if (frame_idx > last_frame_idx_ + step) {
                frames_dropped_.fetch_add(
                    static_cast<std::uint64_t>(
                        frame_idx - last_frame_idx_ - step),
                    std::memory_order_relaxed);
            }
        }
        const int sw = comp_.CanvasWidth();
        const int sh = comp_.CanvasHeight();
        // R9 zero-copy delivery: acquire a pooled VideoFrame and have
        // the compositor write the finished canvas straight into its
        // plane — the refcounted frame then flows to the monitor
        // widget with no further pixel copies. timestamp = tr (the
        // snapped frame time), duration = one project frame.
        auto vframe = frame_pool_.Acquire(
            RenderNs::VideoPixelFormat::kBGRA8,
            RenderNs::VideoSize{sw, sh}, RenderNs::VideoRect{},
            static_cast<std::int64_t>(std::llround(tr * 1000000.0)),
            static_cast<std::int64_t>(
                std::llround(frame_dt_ * 1000000.0)));
        if (!vframe) {
            WarnOnce("preview frame pool acquire failed");
            return;
        }
        // Perf timing uses the REAL steady clock — the injected
        // FrameClock is virtual (tests advance it through sleeps) and
        // would report ~0 ms of work per tick.
        const auto t0 = std::chrono::steady_clock::now();
        std::string err;
        if (!comp_.RenderFrameAtTo(tr, vframe->data(0),
                                   vframe->plane_size(0), &err)) {
            WarnOnce("preview render failed at t=" +
                     std::to_string(t) + ": " + err);
            return;
        }
        const auto t1 = std::chrono::steady_clock::now();
        const double render_ms =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        last_render_ms_.store(render_ms, std::memory_order_relaxed);
        // Adaptive ladder input — only playing renders feed the
        // rolling average (paused full-res inspection frames aren't
        // the measurement). The window evaluator in WorkerMain does
        // the stepping.
        if (playing && options_.adaptive_quality) {
            ema_render_ms_ =
                ema_render_ms_ < 0.0
                    ? render_ms
                    : ema_render_ms_ * (1.0 - kAdaptiveEmaAlpha) +
                          render_ms * kAdaptiveEmaAlpha;
        }
        sink_->OnVideoFrame(std::move(vframe));
        // DidFinishFrame analogue: ack the presented pts so the
        // scheduler can count skipped media slots / missed windows.
        if (sched_) {
            sched_->OnFramePresented(tr);
            const auto sc = sched_->counters();
            sched_dropped_.store(sc.dropped, std::memory_order_relaxed);
        }
        const auto t2 = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lk(mutex_);
            ++perf_.renders;
            perf_.render_ms +=
                std::chrono::duration<double, std::milli>(t1 - t0)
                    .count();
            perf_.deliver_ms +=
                std::chrono::duration<double, std::milli>(t2 - t1)
                    .count();
        }
        ++frames_presented_;
        last_render_t_ = t;
        last_render_tr_ = tr;
        last_frame_idx_ = frame_idx;
    }

    // Drains every mixer chunk index crossed by the playhead (each
    // exactly once — the mixer's sequential contract) into the audio
    // sink + meter callback. Skipped entirely unless playing forward
    // at a listenable rate; the mixer re-opens when the wanted index
    // is unreachable (backward) or wastefully far ahead.
    void PumpAudio(bool playing, double rate, double t,
                   float master_gain_db) {
        if (!options_.audio_enabled || audio_broken_ || !playing ||
            rate <= 0.0 || rate > PreviewEngine::kMaxAudioRate) {
            return;
        }
        if (!mixer_open_) {
            ReopenMixer(t);
            if (!mixer_open_ || audio_chunk_count_ == 0) {
                return;
            }
        }
        const std::size_t want = ChunkIndexAt(t);
        const std::size_t lead_chunks =
            chunk_dur_ > 0.0
                ? static_cast<std::size_t>(
                      std::ceil(kAudioLeadSeconds / chunk_dur_))
                : 0;
        // Reopen only on a REAL discontinuity (a user seek): the
        // playhead moved backward past audio we already produced, or
        // jumped far forward. Free-run buffering legitimately puts
        // audio_next_index_ ahead of `want` by up to `lead` — that is
        // not a discontinuity and must not reopen every tick.
        const bool backward_jump =
            want + lead_chunks < audio_next_index_;
        const bool forward_jump =
            want > audio_next_index_ &&
            want - audio_next_index_ > kAudioGapSkipChunks;
        if (backward_jump || forward_jump) {
            ReopenMixer(t);   // resets audio_next_index_ near `want`
        }
        // Free-running stream: produce up to `lead` chunks AHEAD of the
        // playhead so the device buffer stays topped up even when a
        // frame renders late. Chunks always stream sequentially — the
        // only reposition is the discontinuity reopen above.
        const std::size_t pump_to = want + lead_chunks;
        // Live fader: applied every tick — covers both UI changes and
        // the Init-time reset inside ReopenMixer above.
        mixer_.SetMasterGainDb(master_gain_db);
        FFmpeg::AudioFrameInfo chunk;
        int pumped = 0;
        while (mixer_open_ && audio_next_index_ <= pump_to &&
               audio_next_index_ < audio_chunk_count_ &&
               pumped < kMaxAudioChunksPerTick) {
            std::string err;
            if (!mixer_.GetChunk(audio_next_index_, &chunk, &err)) {
                WarnOnce("audio chunk " +
                         std::to_string(audio_next_index_) +
                         " failed: " + err);
                audio_broken_ = true;
                break;
            }
            ++audio_next_index_;
            ++pumped;
            const auto peaks = mixer_.LastChunkPeaks();
            sink_->OnAudioPeakLevels(peaks.first, peaks.second);
            if (options_.audio_sink != nullptr &&
                chunk.sample_count > 0 && !chunk.data.empty()) {
                const auto* pcm =
                    reinterpret_cast<const std::int16_t*>(
                        chunk.data.data());
                if (options_.audio_sink->Write(pcm, chunk.sample_count,
                                               chunk.sample_rate)) {
                    // AudioClock feed — the master media clock counts
                    // output samples the device consumed.
                    if (audio_ts_ != nullptr) {
                        audio_ts_->WroteAudioSamples(
                            chunk.sample_count);
                    }
                } else {
                    // Include the sink's own reason when it has one —
                    // a bare "dropping" hides device-level causes.
                    const std::string sink_err =
                        options_.audio_sink->LastError();
                    WarnOnce("audio sink rejected a chunk; dropping" +
                             (sink_err.empty()
                                  ? std::string()
                                  : " (" + sink_err + ")"));
                    // Non-fatal — keep pumping; video unaffected.
                }
            }
        }
    }

    // ---- worker loop ---------------------------------------------------

    void WorkerMain() {
        bool playing = false;        // last-applied command state
        double applied_rate = 1.0;
        for (;;) {
            double rate;
            float quality;
            float master_gain_db;
            double loop_in, loop_out;
            bool present;
            bool seek_applied = false;
            bool pause_fresh = false;
            {
                std::unique_lock<std::mutex> lk(mutex_);
                cv_.wait(lk, [this] {
                    return stop_ || playing_cmd_ || seek_pending_ ||
                           present_pending_;
                });
                if (stop_) {
                    return;
                }
                if (seek_pending_) {
                    // Rebase the media clock — works for both paused
                    // presents and mid-play seeks (run state kept).
                    seek_pending_ = false;
                    seek_applied = true;
                    wall_ts_.SetMediaTime(seek_t_);
                    if (sched_) sched_->Reset();
                    // A seek landing never counts as a frame drop.
                    last_frame_idx_ = -1;
                }
                rate = rate_cmd_;
                if (rate != applied_rate) {
                    // Freeze the current estimate into the anchor so a
                    // rate flip never jumps the playhead.
                    wall_ts_.SetPlaybackRate(rate);
                    if (audio_ts_ != nullptr) {
                        audio_ts_->SetPlaybackRate(rate);
                    }
                    applied_rate = rate;
                }
                if (playing_cmd_ && !playing) {
                    // Resume: continue from the last reported playhead.
                    wall_ts_.Start(
                        playhead_.load(std::memory_order_relaxed));
                    // The audio clock resumes at the write frontier
                    // (written-end media position, not the playhead).
                    if (audio_ts_ != nullptr && mixer_open_) {
                        audio_ts_->Start(
                            static_cast<double>(audio_next_index_) *
                            chunk_dur_);
                        last_audio_samples_ = -1;
                    }
                } else if (!playing_cmd_ && playing) {
                    // Pause: freeze the clocks at the current estimate
                    // so paused presents (seek landing, quality
                    // refresh) render the frame under the cursor, not
                    // wherever playback originally anchored.
                    wall_ts_.Pause();
                    if (audio_ts_ != nullptr) {
                        audio_ts_->Pause();
                    }
                    pause_fresh = true;
                }
                playing = playing_cmd_;
                quality = quality_cmd_;
                master_gain_db = master_gain_cmd_;
                loop_in = loop_in_cmd_;
                loop_out = loop_out_cmd_;
                // A fresh pause re-renders the frame under the cursor
                // at full res — Premiere's Paused Resolution (the
                // quality split below makes the render full-scale).
                present = present_pending_ || pause_fresh;
                present_pending_ = false;
            }

            const auto tick_wall0 = std::chrono::steady_clock::now();
            const double tick_begin = clock_->NowSeconds();

            // Paused-vs-playing quality split (Premiere's separate
            // Playback / Paused Resolutions): a playing tick binds
            // the configured tier multiplied by the adaptive ladder
            // step; a paused tick binds full res so inspection
            // frames (focus/edge checks) are pixel-true. The flip
            // rebuilds the scaled render snapshot and re-binds the
            // compositor — canvas dims are cache-key material.
            const float play_scale =
                quality *
                kAdaptiveSteps[std::min(adaptive_level_,
                                        kAdaptiveMaxLevel)];
            const float desired_scale = playing ? play_scale : 1.0f;
            const bool quality_changed = (desired_scale != render_scale_);
            if (quality_changed) {
                render_scale_ = desired_scale;
                render_project_ =
                    ScaledRenderProject(project_, render_scale_);
            }
            // The AutoCacher tracks the PLAYBACK tier only — the
            // paused full-res canvas flip must not re-prime it.
            if (play_scale != autocache_scale_) {
                autocache_scale_ = play_scale;
                autocache_reset_pending_ = true;
            }
            effective_scale_out_.store(render_scale_,
                                       std::memory_order_relaxed);

            const double now = clock_->NowSeconds();
            // BeginFrame tick on the scheduler's vsync grid — produces
            // the deadline window + missed-tick accounting (frames
            // still snap below; the deadline-driven pick is the
            // slice-3 frame-queue work).
            if (sched_) {
                sched_->BeginFrame(now);
                sched_missed_.store(
                    sched_->counters().missed_deadlines,
                    std::memory_order_relaxed);
            }
            double t = wall_ts_.MediaTimeSeconds();
            const bool loop_on = loop_out > loop_in;
            const double bound_lo = loop_on ? loop_in : 0.0;
            const double bound_hi = loop_on ? loop_out : duration_;
            bool hit_end = false;
            if (playing && applied_rate > 0.0 && t >= bound_hi) {
                if (loop_on) {
                    // Wrap keeps the overshoot; the backward landing
                    // triggers a session reopen below.
                    t = bound_lo +
                        std::fmod(t - bound_hi, bound_hi - bound_lo);
                    wall_ts_.SetMediaTime(t);
                } else {
                    t = bound_hi;
                    hit_end = true;
                }
            } else if (playing && applied_rate < 0.0 && t <= bound_lo) {
                if (loop_on) {
                    t = bound_hi -
                        std::fmod(bound_lo - t, bound_hi - bound_lo);
                    wall_ts_.SetMediaTime(t);
                } else {
                    t = bound_lo;
                    hit_end = true;
                }
            }
            t = std::clamp(t, 0.0, duration_);

            // Audio-master drift correction (R8): while the sink is
            // actively consuming chunks the AudioClockTimeSource is
            // authoritative — rebase the wall estimate onto the
            // audible position when it drifts past the threshold.
            // Gated on the sample counter actually advancing this
            // tick (a stalled/broken clock can never pull the
            // playhead), and skipped while looping (the loop wrap
            // intentionally dislocates audio and video clocks).
            if (playing && audio_ts_ != nullptr && !loop_on &&
                !audio_broken_ && applied_rate > 0.0 &&
                applied_rate <= PreviewEngine::kMaxAudioRate) {
                const std::int64_t written =
                    audio_ts_->samples_written();
                if (last_audio_samples_ >= 0 &&
                    written > last_audio_samples_) {
                    const double audible =
                        audio_ts_->MediaTimeSeconds() -
                        kAudioLeadSeconds;
                    if (std::fabs(t - audible) >
                        kAudioDriftCorrectSeconds) {
                        wall_ts_.SetMediaTime(audible);
                        t = std::clamp(wall_ts_.MediaTimeSeconds(),
                                       0.0, duration_);
                    }
                }
                last_audio_samples_ = written;
            }

            // Session continuity — see preview_engine.h for the rule.
            bool need_reopen = !comp_open_ || quality_changed;
            if (!need_reopen && last_render_t_ >= 0.0) {
                const double dt = t - last_render_t_;
                if (dt < -kTimeEpsilon ||
                    dt > PreviewEngine::kForwardReopenThresholdSeconds) {
                    need_reopen = true;
                }
            }
            if (need_reopen) {
                ReopenCompositor();
                last_render_t_ = -1.0;
                last_render_tr_ = -1.0;
                last_frame_idx_ = -1;
                // Discontinuity — the scheduler forgets its outstanding
                // deadline/present anchor so landings never count as
                // drops (same rule as last_frame_idx_ = -1).
                if (sched_) sched_->Reset();
            }
            if (autocache_reset_pending_) {
                autocache_reset_pending_ = false;
                if (autocache_) {
                    std::string ac_err;
                    if (!autocache_->Init(
                            ScaledRenderProject(project_,
                                                autocache_scale_),
                            AutoCacheOptions(),
                            &ac_err)) {
                        WarnOnce("autocache re-init failed: " + ac_err);
                        autocache_.reset();
                    }
                }
            }

            if (playing || present) {
                // Skip ticks whose frame index is unchanged — the
                // snapped render time equals the previous one, so the
                // compositor would produce a byte-identical frame (its
                // per-clip memo already covers the chain, but the
                // blend + canvas copy still cost a full pass).
                const double tr = std::clamp(
                    std::floor(t / frame_dt_ + 1e-9) * frame_dt_, 0.0,
                    duration_);
                const bool same_frame =
                    playing && !present && tr == last_render_tr_;
                // rate == 0 (frozen shuttle) still presents on seeks —
                // plain playing ticks skip the redundant re-render.
                if (!same_frame &&
                    !(playing && applied_rate == 0.0 &&
                      last_render_t_ == t)) {
                    RenderAndDeliver(t, playing, applied_rate);
                }
            }
            // Adaptive ladder evaluation (T4): once per ~500 ms
            // window of playing time, step the tier down while the
            // rolling render average saturates the frame budget, step
            // back up with headroom. Pausing restores level 0 — the
            // measurement restarts on the next play.
            if (playing && options_.adaptive_quality) {
                const auto eval_now = std::chrono::steady_clock::now();
                if (adaptive_window0_.time_since_epoch().count() == 0) {
                    adaptive_window0_ = eval_now;
                }
                if (ema_render_ms_ >= 0.0 &&
                    eval_now - adaptive_window0_ >= kAdaptiveWindow) {
                    adaptive_window0_ = eval_now;
                    const double abs_rate =
                        std::max(std::fabs(applied_rate), 1e-9);
                    const double budget_ms =
                        frame_dt_ / abs_rate * 1000.0;
                    if (ema_render_ms_ > kAdaptiveHigh * budget_ms &&
                        adaptive_level_ < kAdaptiveMaxLevel) {
                        ++adaptive_level_;
                        // Re-measure at the new tier — don't let the
                        // slow frames that caused the step keep it
                        // pinned down.
                        ema_render_ms_ = -1.0;
                    } else if (ema_render_ms_ <
                                   kAdaptiveLow * budget_ms &&
                               adaptive_level_ > 0) {
                        --adaptive_level_;
                        ema_render_ms_ = -1.0;
                    }
                }
            } else if (!playing) {
                adaptive_level_ = 0;
                ema_render_ms_ = -1.0;
                adaptive_window0_ = {};
            }
            // Published AFTER the eval — the worker parks on cv_.wait
            // once paused, so a store left before the eval would pin
            // the stale level forever (the monitor's "adaptive" badge
            // never clears on pause).
            adaptive_level_out_.store(adaptive_level_,
                                      std::memory_order_relaxed);
            const auto audio_t0 = std::chrono::steady_clock::now();
            PumpAudio(playing, applied_rate, t, master_gain_db);
            const auto audio_t1 = std::chrono::steady_clock::now();
            HarvestEngineWarnings();

            // Playhead callback AFTER the frame so the UI sees a
            // coherent (frame, t) pair each tick.
            playhead_.store(t, std::memory_order_relaxed);
            if (autocache_) {
                // Cheap atomic store — lets the worker re-window the
                // pre-render runway ahead of a moving playhead.
                autocache_->OnPlayhead(t);
            }
            sink_->OnPlayhead(t);
            if (seek_applied) {
                // Meters reset on scrubs — stale peaks would linger.
                sink_->OnAudioPeakLevels(0.0f, 0.0f);
            }

            if (hit_end) {
                // Premiere pause-at-end: the playhead holds on the
                // boundary frame; pressing play again there stays put.
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    playing_cmd_ = false;
                }
                playing_.store(false, std::memory_order_relaxed);
                sink_->OnAudioPeakLevels(0.0f, 0.0f);
                continue;   // -> CV wait (flags consumed) = clean pause
            }
            if (playing) {
                // Sink cadence hint (GetPreferredRenderInterval): the
                // media frame duration scaled by |rate| — identical
                // math to the old frame_dt_/|rate| expression.
                const double wall_dt =
                    (sched_ != nullptr &&
                     std::fabs(applied_rate) > 1e-9)
                        ? sched_->preferred_render_interval()
                        : frame_dt_;
                // Budget the WHOLE tick — render + pump + marshal have
                // already consumed part of the frame interval, so
                // sleeping the full wall_dt on top stretched every
                // tick to work+frame_dt and cut the delivered frame
                // rate accordingly.
                const double spent = clock_->NowSeconds() - tick_begin;
                const double remain = wall_dt - spent;
                if (remain > 0.0) {
                    const auto s0 = std::chrono::steady_clock::now();
                    clock_->SleepSeconds(std::min(remain, 0.25));
                    const auto s1 = std::chrono::steady_clock::now();
                    std::lock_guard<std::mutex> lk(mutex_);
                    perf_.sleep_ms +=
                        std::chrono::duration<double, std::milli>(
                            s1 - s0)
                            .count();
                }
            }
            {
                std::lock_guard<std::mutex> lk(mutex_);
                ++perf_.ticks;
                perf_.tick_ms +=
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - tick_wall0)
                        .count();
                perf_.audio_ms +=
                    std::chrono::duration<double, std::milli>(
                        audio_t1 - audio_t0)
                        .count();
            }
            // Paused: next iteration blocks on the CV until a command.
        }
    }
};

// ------------------------------------------------------------------
// PreviewEngine — thin forwarding shell.
// ------------------------------------------------------------------
PreviewEngine::PreviewEngine() : impl_(std::make_unique<Impl>()) {}
PreviewEngine::~PreviewEngine() { impl_->Shutdown(); }

bool PreviewEngine::Init(const Project& snapshot, Sink* sink,
                         std::string* error_out) {
    return impl_->Init(snapshot, PreviewOptions{}, sink, error_out);
}

bool PreviewEngine::Init(const Project& snapshot,
                         const PreviewOptions& options, Sink* sink,
                         std::string* error_out) {
    return impl_->Init(snapshot, options, sink, error_out);
}

void PreviewEngine::Shutdown() { impl_->Shutdown(); }
void PreviewEngine::Seek(double t) { impl_->Seek(t); }
void PreviewEngine::SetPlaying(bool on) { impl_->SetPlaying(on); }
void PreviewEngine::SetRate(double rate) { impl_->SetRate(rate); }
void PreviewEngine::SetLoop(double in_s, double out_s) {
    impl_->SetLoop(in_s, out_s);
}
void PreviewEngine::SetQuality(float scale) {
    impl_->SetQuality(scale);
}
void PreviewEngine::SetMasterGainDb(float db) {
    impl_->SetMasterGainDb(db);
}
float PreviewEngine::MasterGainDb() const {
    return impl_->MasterGainDb();
}
double PreviewEngine::Playhead() const {
    return impl_->playhead_.load(std::memory_order_relaxed);
}
bool PreviewEngine::Playing() const {
    return impl_->playing_.load(std::memory_order_relaxed);
}
double PreviewEngine::DurationSeconds() const { return impl_->duration_; }
std::uint64_t PreviewEngine::FramesPresented() const {
    return impl_->frames_presented_.load(std::memory_order_relaxed);
}
PreviewEngine::FrameStats PreviewEngine::GetFrameStats() const {
    FrameStats s;
    s.rendered = impl_->frames_presented_.load(std::memory_order_relaxed);
    s.dropped = impl_->frames_dropped_.load(std::memory_order_relaxed);
    s.last_render_ms =
        impl_->last_render_ms_.load(std::memory_order_relaxed);
    s.effective_scale =
        impl_->effective_scale_out_.load(std::memory_order_relaxed);
    s.adaptive_level =
        impl_->adaptive_level_out_.load(std::memory_order_relaxed);
    s.scheduler_dropped =
        impl_->sched_dropped_.load(std::memory_order_relaxed) -
        impl_->sched_dropped_base_.load(std::memory_order_relaxed);
    s.scheduler_missed =
        impl_->sched_missed_.load(std::memory_order_relaxed) -
        impl_->sched_missed_base_.load(std::memory_order_relaxed);
    return s;
}
void PreviewEngine::ResetDroppedFrames() {
    impl_->frames_dropped_.store(0, std::memory_order_relaxed);
    // Rebase the scheduler mirrors too — the badge compares the two
    // counters from the same "playback started" origin.
    impl_->sched_dropped_base_.store(
        impl_->sched_dropped_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
    impl_->sched_missed_base_.store(
        impl_->sched_missed_.load(std::memory_order_relaxed),
        std::memory_order_relaxed);
}
std::size_t PreviewEngine::ClipCachedFrames(
    const std::string& clip_id) const {
    return impl_->frame_cache_ ? impl_->frame_cache_->ClipEntryCount(clip_id)
                              : 0;
}
std::vector<std::string> PreviewEngine::Warnings() const {
    std::lock_guard<std::mutex> lk(impl_->mutex_);
    return impl_->warnings_;
}
EffectFrameCache::Stats PreviewEngine::FrameCacheStats() const {
    return impl_->frame_cache_ ? impl_->frame_cache_->GetStats()
                              : EffectFrameCache::Stats{};
}
AutoCacher::Stats PreviewEngine::AutoCacheStats() const {
    return impl_->autocache_ ? impl_->autocache_->GetStats()
                             : AutoCacher::Stats{};
}
PreviewEngine::PerfStats PreviewEngine::GetPerfStats() const {
    std::lock_guard<std::mutex> lk(impl_->mutex_);
    return impl_->perf_;
}
void PreviewEngine::ResetPerfStats() {
    std::lock_guard<std::mutex> lk(impl_->mutex_);
    impl_->perf_ = PerfStats{};
}
bool PreviewEngine::WaitAutoCacheIdle(int timeout_ms) {
    // Disabled -> trivially idle (the check is "no pending work").
    return !impl_->autocache_ ||
           impl_->autocache_->WaitIdle(timeout_ms);
}

}  // namespace XER::VideoEditor
