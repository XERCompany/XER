// PreviewBinding — the ui2 seam between a PreviewEngine and a
// MonitorPanel (spec §6 S2 "frame provider" + the old app's
// VideoEditorAppWidget transport layer, extracted).
//
// What it owns (old-app parity, video_editor_app.cc):
//   * ONE PreviewEngine + its platform audio sink (lazy Init — the
//     engine spins up on the first transport action, Ensure());
//   * the worker→UI marshal: the engine's Sink callbacks only fill
//     pending_* slots under a mutex; DrainPending() (called from the
//     shell's Tick) swaps them out unlocked and applies them to the
//     panel — SetFrame / SetPeakLevels / SetTimecode + surface
//     playhead, plus the engine auto-stop mirror;
//   * transport commands: TogglePlay, JKL ShuttleStep (±{1,2,4,8}
//     ladder — same-direction presses double, the opposite key
//     halves and flips at 1x), ShuttleStop, Seek, StepFrame,
//     ToStart/ToEnd, quality + loop commands;
//   * the cold-start boot sequence (Seek + SetLoop + SetRate +
//     SetPlaying) shared by TogglePlay and ShuttleStep.
//
// What it does NOT own: the exclusive-playback rule between the two
// monitors (the shell stops the sibling binding before starting a
// transport — the two engines share an audio endpoint) and the
// last-active-viewer scope (ShortcutRouter).
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "media_decoder.h"      // MediaDecoderFactory
#include "timeline_model.h"     // Project

namespace XER::VideoEditor {
class PreviewEngine;
struct PreviewOptions;
class IAudioSink;
}

namespace XER::VideoEditor::Ui2 {

class MonitorPanel;

class PreviewBinding {
public:
    PreviewBinding();
    ~PreviewBinding();
    PreviewBinding(const PreviewBinding&) = delete;
    PreviewBinding& operator=(const PreviewBinding&) = delete;

    // ---- wiring ----
    // Not owned — set before the first Ensure(). Drained frames and
    // levels land on the panel.
    void AttachPanel(MonitorPanel* panel);
    // Optional extra playhead fan-out (timeline/inspector in S1+).
    void SetOnPlayhead(std::function<void(double)> cb);
    void SetOnStatus(
        std::function<void(const std::string&, bool)> cb);
    // Proxy seam — forwarded to the compositor's decoder paths
    // (audio always reads original media; PreviewOptions parity).
    void SetMediaResolver(
        std::function<std::string(const std::string&)> resolver);
    // PreviewOptions knobs exposed for tests/headless shells; a
    // null factory keeps the production decoder path.
    void SetDecoderFactory(MediaDecoderFactory factory);
    void SetAutocache(bool enabled);

    // ---- document state ----
    // Rebinds the playback snapshot; the next Ensure() re-inits the
    // engine (Premiere sequence semantics — edits never touch a
    // live session).
    void SetProject(const Project& snapshot);
    const Project& Snapshot() const { return snapshot_; }
    void MarkDirty() { dirty_ = true; }
    // fps driving StepFrame and the shuttle boot (media fps for the
    // source binding, project fps for program).
    void SetFps(int num, int den);
    // Loop state: explicit range when SetLoopRange has out>in, else
    // the pushed work area, else the whole duration (old parity).
    void SetLoopEnabled(bool on);
    bool LoopEnabled() const { return loop_enabled_; }
    void SetLoopRange(double in_seconds, double out_seconds);
    void SetWorkArea(double in_seconds, double out_seconds);
    // Preview render scale (0,1] — Init resets the engine's command
    // so Ensure() re-applies it (old preview_quality_ parity).
    void SetQuality(float scale);
    float Quality() const { return quality_; }
    void SetMasterGainDb(float db);

    // ---- engine lifecycle ----
    // Lazy Init — the old EnsurePreviewEngine/EnsureSourceEngine
    // path. Returns Ready().
    bool Ensure();
    bool Ready() const { return ready_; }
    void Shutdown();

    // ---- transport (UI thread) ----
    void TogglePlay();
    // JKL ladder: ±{1,2,4,8}x. Same-direction presses double, the
    // opposite key halves and flips direction at 1x; a stopped
    // shuttle cold-starts at ±1x (old ShuttleStep parity).
    void ShuttleStep(int direction);   // +1 = L, -1 = J
    // K+J/K+L held combos: plays at a fixed `rate` (bypasses the
    // ladder — Premiere's held slow shuttle). Idempotent under OS
    // key auto-repeat; release is the caller's ShuttleStop().
    void HeldShuttle(double rate);
    void ShuttleStop();                // K — pause + reset ladder
    // Pause at the playhead + reset the ladder (old
    // StopProgramTransport/StopSourceTransport).
    void Stop();
    void Seek(double seconds);
    void StepFrame(int direction);     // ±1 frame at SetFps rate
    void ToStart();
    void ToEnd();

    bool Playing() const;              // ui-facing playing flag
    double Playhead() const { return playhead_; }
    double Duration() const;           // snapshot duration
    double ShuttleRate() const { return shuttle_rate_; }

    // Pure ladder step (unit-tested directly): the rate the NEXT
    // J/L press produces from `current` while `playing`.
    static double NextShuttleRate(double current, int direction,
                                  bool playing);
    static constexpr double kMaxShuttleRate = 8.0;

    // ---- worker→UI marshal ----
    // Call once per shell Tick: swaps pending frame/peaks/playhead
    // out from under the engine worker's mutex and applies them to
    // the attached panel (frame → SetFrame, peaks → SetPeakLevels,
    // playhead → SetTimecode + surface SetTime + on_playhead fanout).
    // Also mirrors engine auto-stop (pause at end / reverse at 0)
    // onto the panel's playing state.
    void DrainPending();

    // ---- pull seams ------------------------------------------------
    // The last engine-reported stereo peak levels (linear
    // amplitude, {L,R}) — read under the marshal mutex. Lets meter
    // consumers that can't ride the panel's SetPeakLevels push (the
    // mixer's master strip) pull the same values. Zeros until the
    // engine reports.
    std::pair<float, float> PeakLevels() const;

private:
    // Forwarding audio sink — engines capture the audio_sink pointer
    // at Init, so a device that appears later reaches the running
    // engine through this binding (old LateBindingAudioSink parity).
    class LateBindingAudioSink;
    class Sink;

    void EnsurePlatformAudioSink();
    // Shared cold-start boot (Seek + SetLoop + SetRate + play flags).
    void StartPlayback(double rate);
    // Single write point for shuttle_rate_ — pushes the speed-badge
    // mirror onto the attached panel (hidden at 0 / +1x).
    void SetShuttleRate(double rate);
    void ApplyLoopCommand();
    void Report(const std::string& text, bool is_error = false);

    MonitorPanel* panel_ = nullptr;
    std::function<void(double)> on_playhead_;
    std::function<void(const std::string&, bool)> on_status_;
    std::function<std::string(const std::string&)> media_resolver_;
    MediaDecoderFactory decoder_factory_;
    bool autocache_ = true;

    Project snapshot_;
    bool dirty_ = true;
    bool ready_ = false;

    std::unique_ptr<PreviewEngine> engine_;
    std::unique_ptr<Sink> sink_;
    std::unique_ptr<IAudioSink> platform_sink_;
    std::unique_ptr<LateBindingAudioSink> sink_binding_;
    bool audio_sink_tried_ = false;
    std::chrono::steady_clock::time_point audio_sink_retry_at_{};

    // Transport state (UI side).
    double playhead_ = 0.0;
    bool ui_playing_ = false;
    double shuttle_rate_ = 0.0;
    float quality_ = 1.0f;
    int fps_num_ = 30, fps_den_ = 1;
    bool loop_enabled_ = false;
    double loop_in_ = 0.0, loop_out_ = 0.0;
    double work_in_ = 0.0, work_out_ = 0.0;

    // Pending marshal slots — written by the engine worker, swapped
    // out by DrainPending (old pending_* parity).
    mutable std::mutex mutex_;
    std::vector<std::uint8_t> pending_frame_;
    int pending_w_ = 0, pending_h_ = 0;
    bool has_frame_ = false;
    float pending_peak_l_ = 0.0f, pending_peak_r_ = 0.0f;
    bool has_peaks_ = false;
    double pending_playhead_ = -1.0;
};

}  // namespace XER::VideoEditor::Ui2
