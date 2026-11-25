// render/frame_scheduler.h — Chromium BeginFrame/viz-DisplayScheduler
// + media::TimeSource analogue for the VideoEditor render port
// (slice 1). See docs/wiki/architecture/chromium-render-port-plan.md
// §4 for the gap table this closes.
//
// Today the preview pump is ad-hoc: PreviewEngine's worker computes
// t = anchor + wallDelta*rate, sleeps frame_dt/|rate|, and renders
// whatever frame the snapped playhead lands on — no deadline, no
// display-interval pick, no backpressure, drops invisible. This class
// is the scheduler half of the Chromium model:
//
//   BeginFrameSource (vsync grid) -> BeginFrameArgs{frame_time,
//   deadline} -> VideoRendererSink::Render(deadline_min, deadline_max)
//   -> the frame whose [pts, pts+dur) covers the display interval.
//
// It produces, per tick, a FrameTick carrying the media-time deadline
// window the due frame must cover plus a drop budget (how many queued
// frames may be expired catching up — VideoRendererAlgorithm's
// RemoveExpiredFrames), and it counts dropped frames / missed
// deadlines the way frames_dropped does upstream.
//
// Deliberately a standalone logic class: no thread, no SetTimer — the
// caller drives it per pump iteration. Replacing the ad-hoc pump is
// ticket R6; this is the unit-coverable math it will call. UI-free.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>

namespace XER::VideoEditor::Render {

// TimeSource — media::TimeSource analogue (src/media/renderers/
// time_source.h sketches the same split as a design doc; this is the
// concrete one the render port uses). Media time in SECONDS; the video
// side derives its render deadlines from it so audio stays the master
// clock (Chromium: audio renderer owns the TimeSource).
class TimeSource {
public:
    virtual ~TimeSource() = default;
    // Current media time, seconds. Monotonic while playing; frozen
    // while paused.
    virtual double MediaTimeSeconds() const = 0;
    virtual double playback_rate() const = 0;
};

// WallClockTimeSource — anchor + wallDelta*rate, i.e. exactly the
// PreviewEngine::Impl anchor_t_/anchor_wall_ math. Default clock is
// std::chrono::steady_clock; tests inject NowFn for determinism.
class WallClockTimeSource final : public TimeSource {
public:
    using NowFn = std::function<double()>;  // monotonic seconds
    WallClockTimeSource();
    explicit WallClockTimeSource(NowFn now_fn);

    void Start(double media_time);    // (re)anchor and run
    void Pause();                     // freeze at the current estimate
    void SetPlaybackRate(double rate);// re-anchors; never jumps media t
    void SetMediaTime(double t);      // rebase without changing run state

    double MediaTimeSeconds() const override;
    double playback_rate() const override { return rate_; }
    bool running() const { return running_; }

private:
    NowFn now_fn_;
    double anchor_media_ = 0.0;
    double anchor_wall_ = 0.0;
    double rate_ = 1.0;
    bool running_ = false;
};

// AudioClockTimeSource — media::AudioClock analogue: media time is the
// count of output samples the audio path has consumed,
//   front_timestamp = base_time + samples_written / sample_rate.
// The preview engine's audio_next_index_*chunk_dur in PumpAudio is
// exactly this — slice 2 wires the sink's sample counter in. Rate is
// metadata for the deadline math (which chunks get written already
// bakes it in — at |rate|>1 the engine time-compresses chunk indices,
// so written samples still track media time 1:1).
class AudioClockTimeSource final : public TimeSource {
public:
    explicit AudioClockTimeSource(int sample_rate);

    // The audio sink consumed `samples` more output samples — callable
    // from the sink/write thread (atomic).
    void WroteAudioSamples(std::int64_t samples);
    // Chromium SetBaseTimestamp semantics: media time origin becomes
    // `media_time` AND the sample accumulator resets — call on seeks.
    void SetBaseTime(double media_time);
    void SetPlaybackRate(double rate);
    void Start(double media_time);    // unpause at media_time
    void Pause();                     // freeze at the current estimate
    bool paused() const { return paused_; }

    double MediaTimeSeconds() const override;
    double playback_rate() const override { return rate_; }
    std::int64_t samples_written() const {
        return samples_written_.load(std::memory_order_relaxed);
    }
    int sample_rate() const { return sample_rate_; }

private:
    int sample_rate_;
    std::atomic<std::int64_t> samples_written_{0};
    double base_ = 0.0;
    double rate_ = 1.0;
    bool paused_ = true;
    double paused_at_ = 0.0;
};

// FrameTick — viz::BeginFrameArgs-lite merged with the
// media::VideoRendererSink::RenderCallback::Render(deadline_min,
// deadline_max) contract: one struct per scheduler tick tells the
// renderer which media-time interval the presented frame must cover
// and how many queued frames it may expire catching up.
struct FrameTick {
    std::uint64_t sequence = 0;
    // Wall seconds: this tick's origin and the next display boundary
    // (vsync grid) the produced frame targets.
    double frame_time_wall = 0.0;
    double display_time_wall = 0.0;
    // Media-time seconds. The due frame's [pts, pts+duration) must
    // cover this window — the Render(deadline_min, deadline_max)
    // contract. Forward rate: [media@display, +interval*rate]; reverse
    // rate flips the window so min<=max always holds.
    double deadline_min = 0.0;
    double deadline_max = 0.0;
    // Expired media-frame slots between the last presented frame and
    // this deadline window — how many queued frames the renderer may
    // skip (VideoRendererAlgorithm's expired-frame drop, expressed as
    // a budget because our queue depth is the caller's concern).
    int drop_budget = 0;
};

class FrameScheduler {
public:
    // `media_clock` not owned; must outlive the scheduler.
    explicit FrameScheduler(TimeSource* media_clock);

    // Display cadence — vsync interval (default 60 Hz). This is the
    // grid display_time_wall snaps to, and the deadline window width.
    void SetDisplayInterval(double seconds);
    // Media frame duration — 1/project_fps (default 30). Used for
    // drop accounting and preferred_render_interval.
    void SetMediaFrameDuration(double seconds);
    // After seeks/reopens/discontinuities: forgets the outstanding
    // deadline and the last-presented anchor so landings never count
    // as drops (same rule as PreviewEngine's last_frame_idx_ = -1).
    void Reset();

    // Produce the tick args for wall-time `wall_now` (seconds; the
    // caller's clock — keeps the class wall-clock-agnostic and
    // deterministic under a manual FrameClock).
    FrameTick BeginFrame(double wall_now_seconds);
    // Renderer ack — the DidFinishFrame analogue. `media_pts` is the
    // presented frame's timestamp in media-time seconds.
    void OnFramePresented(double media_pts_seconds);

    // Sink cadence hint (Chromium GetPreferredRenderInterval): media
    // frame duration scaled by |rate| — how often the pump should tick
    // for this media at this rate.
    double preferred_render_interval() const;

    struct Counters {
        std::uint64_t ticks = 0;
        std::uint64_t presented = 0;
        // Media-frame slots skipped past without a present, beyond the
        // rate's expected step — the frames_dropped analogue.
        std::uint64_t dropped = 0;
        // Ticks whose deadline window fully expired with no present —
        // the BeginFrame-miss metric (viz's missed BeginFrame count).
        std::uint64_t missed_deadlines = 0;
    };
    Counters counters() const { return counters_; }

    // Safety cap on a single tick's drop budget — a huge discontinuity
    // reads as "seek" not "drop everything" (Reset is the real answer
    // for seeks; this keeps a stray clock jump bounded).
    static constexpr int kMaxDropPerTick = 8;

private:
    TimeSource* clock_;
    double vsync_interval_ = 1.0 / 60.0;
    double frame_duration_ = 1.0 / 30.0;
    std::uint64_t sequence_ = 0;
    // Outstanding deadline window from the previous BeginFrame — used
    // to detect the missed-deadline case before it's overwritten.
    bool pending_valid_ = false;
    double pending_deadline_max_ = 0.0;
    bool presented_since_tick_ = false;
    // Last presented frame's media pts — the drop-counting anchor.
    bool last_presented_valid_ = false;
    double last_presented_media_ = 0.0;
    Counters counters_;
};

}  // namespace XER::VideoEditor::Render
