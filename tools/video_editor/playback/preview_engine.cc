// preview_engine.cc — see preview_engine.h for the contract. All
// pixel/audio work happens on the single worker thread; the mutex
// below guards ONLY the command block (UI -> worker), never the
// render path, so sink callbacks can never deadlock on engine state.
#include "preview_engine.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <utility>

#include "wrapper/ffmpeg/ffmpeg_engine_bridge.h"   // AudioFrameInfo

namespace XER::VideoEditor {
namespace {
namespace FFmpeg = XER::Bridge::FFmpeg;

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

// Nearest-neighbor BGRA8 downsample for the quality post-pass — both
// buffers are packed stride==width*4.
void DownsampleNearestBGRA(const std::uint8_t* src, int sw, int sh,
                           std::uint8_t* dst, int dw, int dh) {
    for (int y = 0; y < dh; ++y) {
        const int sy = std::min(sh - 1, (y * sh) / dh);
        const std::uint8_t* srow =
            src + static_cast<std::size_t>(sy) * sw * 4u;
        std::uint8_t* drow = dst + static_cast<std::size_t>(y) * dw * 4u;
        for (int x = 0; x < dw; ++x) {
            const int sx = std::min(sw - 1, (x * sw) / dw);
            std::memcpy(drow + x * 4, srow + sx * 4, 4);
        }
    }
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
    // Shared effect-chain result cache — injected into every
    // compositor session this engine opens. Survives ReopenCompositor
    // (every backward seek / loop wrap re-binds the compositor), so
    // scrubbing back over already-rendered frames hits the cache
    // instead of re-decoding + re-running stacked effects. Entries
    // carry a full input hash, so surviving project edits is also
    // safe — changed inputs simply miss.
    std::shared_ptr<EffectFrameCache> frame_cache_ =
        std::make_shared<EffectFrameCache>();
    // Olive-style idle-time pre-render into frame_cache_ — owns a
    // SECOND compositor (the only safe seam: Compositor is single-
    // consumer-thread), fills the shared cache on its own worker so
    // the playback ticks below hit. Null when disabled.
    std::unique_ptr<AutoCacher> autocache_;
    AudioMixer mixer_;
    bool comp_open_ = false;
    bool mixer_open_ = false;
    bool audio_broken_ = false;         // GetChunk hard-failed; stop pump
    double anchor_t_ = 0.0;             // playhead at anchor_wall_
    double anchor_wall_ = 0.0;          // clock seconds at anchor
    double last_render_t_ = -1.0;       // <0: nothing rendered this session
    std::size_t audio_next_index_ = 0;  // next chunk index to produce
    std::size_t audio_chunk_count_ = 0;
    double chunk_dur_ = 0.0;            // chunk_samples / sample_rate
    std::vector<std::uint8_t> bgra_;    // compositor output scratch
    video::Frame out_frame_;            // delivery frame (reused)

    // ---- UI-readable atomics ----
    std::atomic<double> playhead_{0.0};
    std::atomic<bool> playing_{false};
    std::atomic<std::uint64_t> frames_presented_{0};

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
        anchor_t_ = 0.0;
        anchor_wall_ = 0.0;
        last_render_t_ = -1.0;
        audio_next_index_ = 0;
        audio_chunk_count_ = 0;
        chunk_dur_ = 0.0;
        playhead_.store(0.0, std::memory_order_relaxed);
        playing_.store(false, std::memory_order_relaxed);
        frames_presented_.store(0, std::memory_order_relaxed);
        // AutoCacher: pre-renders the playhead window into
        // frame_cache_ on its own low-priority thread + private
        // compositor. Same snapshot + factory, same cache — playback
        // renders then hit instead of re-decoding. Init failures are
        // non-fatal (the feature degrades to plain playback).
        if (options_.autocache && AutoCacher::EnabledByEnv() &&
            frame_cache_->CapacityBytes() > 0) {
            AutoCacher::Options aco;
            aco.decoder_factory = factory_;
            aco.frame_cache = frame_cache_;
            aco.media_resolver = options_.media_resolver;
            auto ac = std::make_unique<AutoCacher>();
            std::string ac_err;
            if (ac->Init(project_, aco, &ac_err)) {
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
        if (!comp_.Init(project_, opts, &err, &w)) {
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

    void RenderAndDeliver(double t, float quality) {
        if (!comp_open_) {
            return;
        }
        // Preview-quality hint: <1.0 runs the heavy clip effects on a
        // downscaled frame inside the compositor (export/screenshot
        // compositors never set this — full quality stays bit-exact).
        comp_.SetPreviewQuality(quality);
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
        std::string err;
        if (!comp_.RenderFrameAt(tr, &bgra_, &err)) {
            WarnOnce("preview render failed at t=" +
                     std::to_string(t) + ": " + err);
            return;
        }
        const int sw = comp_.CanvasWidth();
        const int sh = comp_.CanvasHeight();
        const float q = (std::isfinite(quality) && quality > 0.0f &&
                         quality < 1.0f)
                            ? quality
                            : 1.0f;
        const int tw =
            std::max(1, static_cast<int>(std::lround(sw * q)));
        const int th =
            std::max(1, static_cast<int>(std::lround(sh * q)));
        if (!out_frame_.IsValid() || out_frame_.Width() != tw ||
            out_frame_.Height() != th ||
            out_frame_.Format() != video::PixelFormat::BGRA8) {
            out_frame_.Release();
            if (!out_frame_.Allocate(tw, th, video::PixelFormat::BGRA8)) {
                WarnOnce("preview output frame allocation failed");
                return;
            }
        }
        if (tw == sw && th == sh) {
            std::memcpy(out_frame_.Data(), bgra_.data(), bgra_.size());
        } else {
            DownsampleNearestBGRA(bgra_.data(), sw, sh,
                                  out_frame_.Data(), tw, th);
        }
        // pts reports the rendered frame's timeline position (tr —
        // the snapped frame time), not the raw wall-clock estimate.
        out_frame_.SetPts(static_cast<std::int64_t>(
            std::llround(tr * 1000000.0)));
        out_frame_.Meta().fps =
            static_cast<double>(project_.fps_num) / project_.fps_den;
        sink_->OnVideoFrame(out_frame_);
        ++frames_presented_;
        last_render_t_ = t;
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
                if (!options_.audio_sink->Write(pcm, chunk.sample_count,
                                                chunk.sample_rate)) {
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
                    // Rebase the wall-clock anchor — works for both
                    // paused presents and mid-play seeks.
                    seek_pending_ = false;
                    seek_applied = true;
                    anchor_t_ = seek_t_;
                    anchor_wall_ = clock_->NowSeconds();
                }
                rate = rate_cmd_;
                if (rate != applied_rate) {
                    // Freeze the current estimate into the anchor so a
                    // rate flip never jumps the playhead.
                    const double n = clock_->NowSeconds();
                    const double cur =
                        playing
                            ? anchor_t_ + (n - anchor_wall_) * applied_rate
                            : anchor_t_;
                    anchor_t_ = cur;
                    anchor_wall_ = n;
                    applied_rate = rate;
                }
                if (playing_cmd_ && !playing) {
                    // Resume: continue from the last reported playhead.
                    anchor_t_ =
                        playhead_.load(std::memory_order_relaxed);
                    anchor_wall_ = clock_->NowSeconds();
                } else if (!playing_cmd_ && playing) {
                    // Pause: freeze the anchor at the current estimate
                    // so paused presents (seek landing, quality
                    // refresh) render the frame under the cursor, not
                    // wherever playback originally anchored.
                    const double n = clock_->NowSeconds();
                    anchor_t_ =
                        anchor_t_ + (n - anchor_wall_) * applied_rate;
                    anchor_wall_ = n;
                }
                playing = playing_cmd_;
                quality = quality_cmd_;
                master_gain_db = master_gain_cmd_;
                loop_in = loop_in_cmd_;
                loop_out = loop_out_cmd_;
                present = present_pending_;
                present_pending_ = false;
            }

            const double now = clock_->NowSeconds();
            double t = anchor_t_;
            if (playing && applied_rate != 0.0) {
                t = anchor_t_ + (now - anchor_wall_) * applied_rate;
            }
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
                    anchor_t_ = t;
                    anchor_wall_ = now;
                } else {
                    t = bound_hi;
                    hit_end = true;
                }
            } else if (playing && applied_rate < 0.0 && t <= bound_lo) {
                if (loop_on) {
                    t = bound_hi -
                        std::fmod(bound_lo - t, bound_hi - bound_lo);
                    anchor_t_ = t;
                    anchor_wall_ = now;
                } else {
                    t = bound_lo;
                    hit_end = true;
                }
            }
            t = std::clamp(t, 0.0, duration_);

            // Session continuity — see preview_engine.h for the rule.
            bool need_reopen = !comp_open_;
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
            }

            if (playing || present) {
                // rate == 0 (frozen shuttle) still presents on seeks —
                // plain playing ticks skip the redundant re-render.
                if (!(playing && applied_rate == 0.0 &&
                      last_render_t_ == t)) {
                    RenderAndDeliver(t, quality);
                }
            }
            PumpAudio(playing, applied_rate, t, master_gain_db);
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
                const double abs_rate = std::fabs(applied_rate);
                const double wall_dt = abs_rate > 1e-9
                                           ? frame_dt_ / abs_rate
                                           : frame_dt_;
                clock_->SleepSeconds(std::clamp(wall_dt, 0.001, 0.25));
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
bool PreviewEngine::WaitAutoCacheIdle(int timeout_ms) {
    // Disabled -> trivially idle (the check is "no pending work").
    return !impl_->autocache_ ||
           impl_->autocache_->WaitIdle(timeout_ms);
}

}  // namespace XER::VideoEditor
