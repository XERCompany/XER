// Offline export worker for the VideoEditor core (XER Studio Phase
// 1C). UI-free; Start() snapshots the project and spawns one worker
// thread that drives Compositor -> video provider and AudioMixer ->
// audio provider through the FFmpeg streaming encoders. Output is
// written to a temp file next to the target and committed atomically.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "audio_mixer.h"
#include "compositor.h"
#include "timeline_model.h"

namespace XER::VideoEditor {

struct ExportOptions {
    std::string output_path;       // UTF-8; container from extension
                                   // (.mp4 default mux)
    std::string video_codec = "libx264";
    std::string audio_codec = "aac";
    int video_bit_rate = 8000000;  // 0 -> bridge default (4 Mbps)
    int audio_bit_rate = 128000;   // 0 -> bridge default (192k)
    // Post-mix master gain in dB applied by the audio mixer before the
    // output clamp — the export-time equivalent of the preview master
    // fader. 0 = unity.
    float master_gain_db = 0.0f;
    // Output resolution multiplier applied to the project canvas —
    // 1.0 (default) = native size, 0.5 = half dimensions (draft).
    // The engine renders at round(dim*scale) snapped to even values
    // and scales clip transform.scale accordingly; Start rejects
    // non-finite/non-positive values or results under 2px.
    double resolution_scale = 1.0;
    // Explicit export range in timeline seconds [in, out). When
    // range_out <= range_in (the default) the project's work area
    // (work_in/work_out) is used instead, and no work area means the
    // whole project. A nonzero explicit range overrides the work
    // area. Values are clamped to [0, project.duration()].
    double range_in = 0.0;
    double range_out = 0.0;
    // Canvas/fps come from the Project — not options.
};

enum class ExportStatus { kIdle, kRunning, kDone, kCancelled, kError };

struct ExportProgress {
    double fraction = 0.0;         // [0,1], monotonic non-decreasing
    std::uint64_t video_frames_done = 0;
    std::uint64_t video_frames_total = 0;
    std::uint64_t audio_chunks_done = 0;
    std::uint64_t audio_chunks_total = 0;
    std::string stage = "init";    // init|video|audio|mux|commit
};

// Offline frame-accurate export worker. UI-free; Start() snapshots
// the project and spawns ONE std::thread; all encode work happens
// there. Wait() joins (the destructor Cancel()+Wait()s — never leaks a
// worker touching a dead engine).
class ExportEngine {
public:
    ExportEngine();
    ~ExportEngine();
    ExportEngine(const ExportEngine&) = delete;
    ExportEngine& operator=(const ExportEngine&) = delete;

    // Validates + snapshots `project`, probes unique source paths via
    // ProbeVideo to decide has_video/has_audio, computes stream counts,
    // and spawns the worker. Returns false + error_out on: empty
    // duration, empty output path, FFmpeg unavailable, probe failure
    // (missing media fails FAST — before any encode), invalid fps, or
    // already running. `progress` (optional) is invoked on the worker
    // thread — marshal to UI before touching widgets.
    bool Start(const Project& project, const ExportOptions& options,
               std::function<void(const ExportProgress&)> progress = {},
               std::string* error_out = nullptr);

    void Cancel();        // cooperative; any thread; idempotent
    bool Wait();          // blocks until worker exits; true if joined
    bool IsRunning() const;
    ExportStatus Status() const;
    ExportProgress Progress() const;   // latest snapshot
    std::string Error() const;  // meaningful when kError

private:
    struct MediaProbe {
        bool has_video = false;
        bool has_audio = false;
    };

    void Run();
    void RunImpl();
    void Report(const char* stage);
    void UpdateDone(std::uint64_t video_done, std::uint64_t audio_done,
                    const char* stage);
    void FinishCancelled();
    void FinishError(const std::string& error);
    void ShutdownEngines();

    static bool FailStart(std::string* error_out, const std::string& error);

    Project project_;
    ExportOptions options_;
    std::function<void(const ExportProgress&)> progress_cb_;

    Compositor compositor_;
    AudioMixer mixer_;
    bool has_video_ = false;
    bool has_audio_ = false;

    std::thread worker_;
    std::mutex worker_mutex_;
    std::atomic<bool> cancel_requested_{false};
    std::atomic<ExportStatus> status_{ExportStatus::kIdle};

    mutable std::mutex progress_mutex_;
    ExportProgress progress_;
    std::string error_;
};

}  // namespace XER::VideoEditor
