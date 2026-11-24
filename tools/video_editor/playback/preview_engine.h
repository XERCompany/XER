// Realtime program-monitor playback engine for the VideoEditor core
// (Phase 2). UI-free: Init() snapshots the project and owns ONE
// worker thread that walks the timeline by wall clock * rate, renders
// canvas BGRA through Compositor, pumps AudioMixer chunks into a
// caller-supplied IAudioSink, and reports video frames / audio meters
// / playhead through a caller-supplied Sink. Edits after Init do not
// affect playback until the next Init (Premiere sequence semantics).
//
// Session-continuity model — Compositor::RenderFrameAt and
// AudioMixer::GetChunk both assume NON-DECREASING time per session:
//   * normal forward playback and small forward seeks keep one
//     session alive — decoders run sequentially forward, the
//     frame-accurate fast path;
//   * ANY backward movement (backward Seek, every negative-rate
//     shuttle tick, loop wrap to a lower t) re-opens the session
//     (Shutdown + Init on the same snapshot) so decoders re-seek to
//     ~0.25 s before the target instead of serving stale frames;
//   * forward jumps larger than kForwardReopenThresholdSeconds also
//     re-open — a decoder Seek is cheaper than decoding through
//     seconds of pictures.
// Reopen cost is one decoder open + a short forward discard per
// visible clip: acceptable for v2 scrubbing/shuttling (a future
// optimization could keep a dedicated scrub compositor or add a true
// random-access decode path). The mixer's chunk index is tracked
// SEPARATELY — it re-opens only when the wanted index is no longer
// reachable (backward) or too far ahead, so negative-rate stretches
// don't thrash audio sessions.
//
// Audio: pumped only while 0 < rate <= kMaxAudioRate (4x) — no
// reverse or extreme-shuttle audio in v2; the video clock still steps
// under negative rates, audio just mutes. Every chunk index crossed
// by the playhead is produced exactly once in order, so |rate| > 1
// plays time-compressed audio (chunk cadence follows the playhead,
// not wall clock) — Premiere JKL behavior, close enough for preview.
// The engine never resamples; the sink receives the mixer's native
// rate (a blocking IAudioSink::Write paces the pump for free).
//
// Threading: all public methods are UI-thread calls; every Sink and
// IAudioSink callback fires on the worker thread (marshal to the UI
// thread before touching widgets). Shutdown() joins the worker — no
// callbacks fire after it returns. The Sink and IAudioSink are not
// owned and must outlive the engine.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "audio_mixer.h"
#include "auto_cacher.h"     // AutoCacher::Stats (PUBLIC dep)
#include "compositor.h"
#include "frame.h"            // video::Frame — XER_Video (PUBLIC dep)
#include "frame_clock.h"
#include "audio_output.h"
#include "media_decoder.h"
#include "timeline_model.h"

namespace XER::VideoEditor {

struct PreviewOptions {
    // Empty -> DefaultDecoderFactory() (real FFmpeg bridge). The same
    // factory feeds BOTH the compositor and the mixer — they open
    // independent decoder instances per clip (same as export). Tests
    // inject fakes.
    MediaDecoderFactory decoder_factory;
    // Null -> system clock. Tests inject a manual FrameClock so the
    // playhead advance is deterministic.
    std::shared_ptr<FrameClock> clock;
    // Not owned; may be null (audio is still mixed for the meters —
    // chunks are produced and peaks reported, just not written).
    IAudioSink* audio_sink = nullptr;
    int sample_rate = 48000;          // mixer output rate (s16 stereo)
    int audio_chunk_samples = 1024;   // mixer/provider chunk size
    bool audio_enabled = true;        // false -> video-only pump
    // Olive-style idle-time pre-render around the playhead into the
    // shared EffectFrameCache (playback/auto_cacher.h). Also gated by
    // the XER_VE_AUTOCACHE env var (=0 disables); effective enable is
    // this flag AND the env. Default ON — pre-rendering is the only
    // way wall-clock playback frames become cache hits.
    bool autocache = true;
    // Optional media-path resolver forwarded to the compositor (and
    // the autocache's private compositor) — the proxy seam: maps
    // clip.source_path to its ready proxy for VIDEO decode. The audio
    // mixer never sees it: proxies carry no audio, so sound always
    // comes off the original media. Empty -> original paths.
    std::function<std::string(const std::string&)> media_resolver;
};

class PreviewEngine {
public:
    // UI-facing callbacks — all fired on the worker thread.
    struct Sink {
        virtual ~Sink() = default;
        // Latest composited canvas, packed BGRA8 (scaled down when
        // quality < 1). pts is the timeline time in microseconds.
        virtual void OnVideoFrame(const video::Frame& frame) = 0;
        // Stereo peak magnitudes of the last mixed chunk (may exceed
        // 1.0 on overs). Fired once per written chunk; {0,0} on
        // pause-at-end and after seeks so meters decay.
        virtual void OnAudioPeakLevels(float l, float r) = 0;
        // Wall-clock-driven playhead, fired once per worker tick
        // (after the frame callback so the UI can pair them).
        virtual void OnPlayhead(double t) = 0;
    };

    PreviewEngine();
    ~PreviewEngine();   // Shutdown()
    PreviewEngine(const PreviewEngine&) = delete;
    PreviewEngine& operator=(const PreviewEngine&) = delete;

    // Default options: real decoders, system clock, no audio sink.
    bool Init(const Project& snapshot, Sink* sink,
              std::string* error_out = nullptr);
    // Full options — tests inject decoder_factory + clock + sink.
    bool Init(const Project& snapshot, const PreviewOptions& options,
              Sink* sink, std::string* error_out = nullptr);
    void Shutdown();   // joins the worker, closes decoders; idempotent

    // Absolute seek in timeline seconds (clamped to [0, duration]).
    // Paused: presents one frame at t. Playing: rebases the clock and
    // keeps going from t. Any backward landing point re-opens decode
    // sessions — see the header notes.
    void Seek(double t);
    void SetPlaying(bool on);          // pause keeps the playhead
    // J/K/L shuttle: 1 forward, 2 fast, -1/-2 reverse. Non-finite
    // rates are ignored; rate 0 freezes the playhead without pausing.
    void SetRate(double rate);
    // Loop [in_s, out_s); in_s >= out_s disables looping. Values are
    // clamped to [0, duration]. Wrapping re-opens sessions per the
    // continuity rules above.
    void SetLoop(double in_s, double out_s);
    // Preview render scale in (0,1] — values > 1 or non-finite clamp
    // to 1 (full res). Scales the CANVAS the compositor renders to
    // (the warp, blend, canvas and delivery copies all shrink
    // quadratically — decode still runs at source resolution), so
    // the tier is honest render work reduction, not a display
    // downsample. Canvas dims are effect-cache key material, so a
    // tier flip re-binds the compositor and re-primes the AutoCacher.
    void SetQuality(float scale);
    // Master fader in dB — forwarded to the audio mixer and applied
    // to the mixed output BEFORE the peak read, so OnAudioPeakLevels
    // reflects it. Non-finite values are ignored; Init() resets to 0.
    void SetMasterGainDb(float db);
    float MasterGainDb() const;

    double Playhead() const;
    bool Playing() const;
    double DurationSeconds() const;
    // Completed OnVideoFrame deliveries — tests/monitoring.
    std::uint64_t FramesPresented() const;
    // Accumulated non-fatal notes (compositor + mixer, dedup'd).
    std::vector<std::string> Warnings() const;

    // Effect-chain cache counters for the engine's shared
    // EffectFrameCache (hits/misses/stores/evictions) — the same
    // object the AutoCacher fills. Lightweight; for perf HUDs and
    // smoke logs.
    EffectFrameCache::Stats FrameCacheStats() const;
    // AutoCacher counters (windows/queued/done/skipped/cancelled/
    // failed) — all zeros when the feature is disabled.
    AutoCacher::Stats AutoCacheStats() const;
    // Coarse per-stage wall-clock totals accumulated by the worker —
    // the "why is preview slow" breakdown. `render` is the compositor
    // call (decode + effect chain + warp + blend happen inside it),
    // `deliver` is the frame wrap/copy + Sink::OnVideoFrame marshal,
    // `audio` is the mixer pump, `tick` is the whole loop iteration.
    // Monotonic from Init; ResetPerfStats() re-zeroes for a
    // measurement window (smoke checks, perf HUDs).
    struct PerfStats {
        std::uint64_t ticks = 0;      // worker loop iterations
        std::uint64_t renders = 0;    // RenderAndDeliver attempts
        double render_ms = 0.0;       // comp_.RenderFrameAt total
        double deliver_ms = 0.0;      // frame wrap + OnVideoFrame total
        double audio_ms = 0.0;        // PumpAudio total
        double tick_ms = 0.0;         // full tick (work, excl. sleep)
        double sleep_ms = 0.0;        // idle pacing total
    };
    PerfStats GetPerfStats() const;
    void ResetPerfStats();
    // Waits until the AutoCacher queue is drained and no window is
    // pending (up to timeout_ms) — tests/smoke only; returns false on
    // timeout or when disabled-but-idle (idle is always true then:
    // the wait trivially succeeds).
    bool WaitAutoCacheIdle(int timeout_ms);

    // Forward jumps larger than this re-open decode sessions instead
    // of sequential-decoding through the gap (visible for tuning).
    static constexpr double kForwardReopenThresholdSeconds = 0.5;
    // Audio mutes above this |rate| (JKL fast-shuttle cut-off).
    static constexpr double kMaxAudioRate = 4.0;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace XER::VideoEditor
