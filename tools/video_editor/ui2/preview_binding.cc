#include "preview_binding.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <utility>

#include "playback/audio_sink_factory.h"  // MakePlatformAudioSink
#include "monitor_panel.h"
#include "playback/audio_output.h"    // IAudioSink
#include "playback/preview_engine.h"

namespace XER::VideoEditor::Ui2 {

// ---- LateBindingAudioSink ----------------------------------------------------
// Ported verbatim from ui/video_editor_app.h: the engines capture
// their IAudioSink pointer once at Init, so a device that appears
// LATER (retry path, headphones plugged in) could never reach an
// already-initialized engine through a raw pointer. The binding is
// atomic: the worker can sit inside Write while the UI thread swaps
// the target.
class PreviewBinding::LateBindingAudioSink final : public IAudioSink {
public:
    void Bind(IAudioSink* target) {
        target_.store(target, std::memory_order_release);
    }
    bool Write(const std::int16_t* pcm, int frames,
               int rate) override {
        IAudioSink* t = target_.load(std::memory_order_acquire);
        return t != nullptr && t->Write(pcm, frames, rate);
    }
    std::string LastError() const override {
        IAudioSink* t = target_.load(std::memory_order_acquire);
        return t != nullptr ? t->LastError()
                            : std::string("audio device not open");
    }

private:
    std::atomic<IAudioSink*> target_{nullptr};
};

// ---- Sink: worker-thread callbacks -> pending slots ---------------------------
class PreviewBinding::Sink final : public PreviewEngine::Sink {
public:
    explicit Sink(PreviewBinding* owner) : owner_(owner) {}

    void OnVideoFrame(
        Render::scoped_refptr<Render::VideoFrame> frame) override {
        // Refcount hand-off — the frame's plane is already the
        // composited canvas; a newer pending frame just drops its
        // ref (storage returns to the engine's pool when the last
        // consumer releases).
        if (!frame || frame->planes() < 1 ||
            frame->coded_size().IsEmpty()) {
            return;
        }
        auto* o = owner_;
        std::lock_guard<std::mutex> lock(o->mutex_);
        o->pending_frame_ = std::move(frame);
        o->has_frame_ = true;
    }
    void OnAudioPeakLevels(float l, float r) override {
        auto* o = owner_;
        std::lock_guard<std::mutex> lock(o->mutex_);
        o->pending_peak_l_ = l;
        o->pending_peak_r_ = r;
        o->has_peaks_ = true;
    }
    void OnPlayhead(double t) override {
        auto* o = owner_;
        std::lock_guard<std::mutex> lock(o->mutex_);
        o->pending_playhead_ = t;
    }

private:
    PreviewBinding* owner_;   // binding outlives the engine worker
};

// ---- lifecycle -----------------------------------------------------------------

PreviewBinding::PreviewBinding()
    : engine_(std::make_unique<PreviewEngine>()),
      sink_binding_(std::make_unique<LateBindingAudioSink>()) {}

PreviewBinding::~PreviewBinding() { Shutdown(); }

void PreviewBinding::AttachPanel(MonitorPanel* panel) {
    panel_ = panel;
    if (panel_ != nullptr) {
        panel_->SetFps(fps_num_, fps_den_);
    }
}

void PreviewBinding::SetOnPlayhead(std::function<void(double)> cb) {
    on_playhead_ = std::move(cb);
}
void PreviewBinding::SetOnStatus(
    std::function<void(const std::string&, bool)> cb) {
    on_status_ = std::move(cb);
}
void PreviewBinding::SetMediaResolver(
    std::function<std::string(const std::string&)> resolver) {
    media_resolver_ = std::move(resolver);
}
void PreviewBinding::SetDecoderFactory(MediaDecoderFactory factory) {
    decoder_factory_ = std::move(factory);
}
void PreviewBinding::SetAutocache(bool enabled) {
    autocache_ = enabled;
}
void PreviewBinding::SetClock(std::shared_ptr<FrameClock> clock) {
    clock_ = std::move(clock);
}
void PreviewBinding::SetFps(int num, int den) {
    fps_num_ = num > 0 ? num : 30;
    fps_den_ = den > 0 ? den : 1;
    if (panel_ != nullptr) panel_->SetFps(fps_num_, fps_den_);
}
void PreviewBinding::SetLoopEnabled(bool on) { loop_enabled_ = on; }
void PreviewBinding::SetLoopRange(double in_s, double out_s) {
    loop_in_ = in_s;
    loop_out_ = out_s;
}
void PreviewBinding::SetWorkArea(double in_s, double out_s) {
    work_in_ = in_s;
    work_out_ = out_s;
}
void PreviewBinding::SetQuality(float scale) {
    quality_ = std::clamp(scale, 0.0f, 1.0f);
    if (quality_ <= 0.0f) quality_ = 1.0f;
    if (ready_) engine_->SetQuality(quality_);
}
void PreviewBinding::SetMasterGainDb(float db) {
    if (ready_) engine_->SetMasterGainDb(db);
}
void PreviewBinding::SetProject(const Project& snapshot) {
    snapshot_ = snapshot;
    dirty_ = true;
    playhead_ = 0.0;
    ui_playing_ = false;
    SetShuttleRate(0.0);
}

double PreviewBinding::Duration() const { return snapshot_.duration(); }

void PreviewBinding::Report(const std::string& text, bool is_error) {
    if (on_status_) on_status_(text, is_error);
}

// ---- engine lifecycle -----------------------------------------------------------

void PreviewBinding::EnsurePlatformAudioSink() {
    if (platform_sink_) return;
    // A failed open doesn't latch — retry throttled so late-arriving
    // devices (headphones, USB DAC) still bind on a later Ensure.
    const auto now = std::chrono::steady_clock::now();
    if (audio_sink_tried_ && now < audio_sink_retry_at_) return;
    audio_sink_tried_ = true;
    audio_sink_retry_at_ = now + std::chrono::seconds(3);
    std::string error;
    platform_sink_ = MakePlatformAudioSink(&error);
    if (platform_sink_ != nullptr) {
        sink_binding_->Bind(platform_sink_.get());
    } else if (!error.empty()) {
        Report("Audio preview unavailable: " + error);
    }
}

bool PreviewBinding::Ensure() {
    if (ready_ && !dirty_) return true;
    engine_->Shutdown();
    ready_ = false;
    if (sink_ == nullptr) sink_ = std::make_unique<Sink>(this);
    EnsurePlatformAudioSink();
    PreviewOptions options;
    options.audio_sink = sink_binding_.get();
    options.autocache = autocache_;
    if (decoder_factory_) options.decoder_factory = decoder_factory_;
    if (clock_) options.clock = clock_;
    if (media_resolver_) options.media_resolver = media_resolver_;
    std::string error;
    ready_ = engine_->Init(snapshot_, options, sink_.get(), &error);
    if (ready_) {
        // Init resets the quality + master-gain commands — re-apply.
        engine_->SetQuality(quality_);
        engine_->SetMasterGainDb(snapshot_.master_gain_db);
    } else {
        Report("Preview init failed: " + error, true);
    }
    dirty_ = false;
    return ready_;
}

void PreviewBinding::Shutdown() {
    if (engine_ != nullptr) engine_->Shutdown();
    ready_ = false;
    ui_playing_ = false;
    SetShuttleRate(0.0);
}

// ---- transport --------------------------------------------------------------------

void PreviewBinding::SetShuttleRate(double rate) {
    shuttle_rate_ = rate;
    if (panel_ != nullptr) panel_->SetSpeedBadge(rate);
}

void PreviewBinding::ApplyLoopCommand() {
    // Loop honors the explicit range, else the pushed work area; the
    // engine resolves in>=out as "no loop" (old parity).
    double li = 0.0, lo = 0.0;
    if (loop_enabled_) {
        li = loop_in_;
        lo = loop_out_;
        if (lo <= li) {
            li = work_in_;
            lo = work_out_;
        }
    }
    engine_->SetLoop(li, lo);
}

void PreviewBinding::StartPlayback(double rate) {
    // Cold-start boot — the same sequence TogglePlay's play branch
    // ran in the old app: seek to the UI playhead, apply the loop
    // command, then the rate + play flags.
    const double duration = Duration();
    if (rate > 0.0 && duration > 0.0 &&
        playhead_ >= duration - 1e-6) {
        playhead_ = 0.0;
    }
    engine_->Seek(playhead_);
    ApplyLoopCommand();
    engine_->SetRate(rate);
    engine_->SetPlaying(true);
    // Premiere resets the dropped-frame indicator on every play.
    engine_->ResetDroppedFrames();
    ui_playing_ = true;
    SetShuttleRate(rate);
    if (panel_ != nullptr) panel_->SetPlaying(true);
}

void PreviewBinding::TogglePlay() {
    const bool next = !ui_playing_;
    SetShuttleRate(0.0);   // Space always returns to normal 1x
    if (next) {
        if (!Ensure()) {
            Report("Preview engine unavailable", true);
            return;
        }
        StartPlayback(1.0);
    } else {
        Stop();
    }
}

double PreviewBinding::NextShuttleRate(double current, int direction,
                                       bool playing) {
    double rate = current;
    if (!playing) {
        return static_cast<double>(direction);  // stopped -> ±1x
    }
    if (rate == 0.0) rate = 1.0;   // normal play = +1 on the ladder
    if ((rate > 0.0) == (direction > 0)) {
        // Same direction: double, capped at 8x.
        rate = direction > 0
                   ? std::min(rate * 2.0, kMaxShuttleRate)
                   : std::max(rate * 2.0, -kMaxShuttleRate);
    } else {
        // Opposite direction: halve toward zero; at 1x the press
        // reverses onto the other side of the ladder.
        const double mag = std::fabs(rate);
        rate = mag <= 1.0 ? static_cast<double>(direction)
                          : rate * 0.5;
    }
    return rate;
}

void PreviewBinding::ShuttleStep(int direction) {
    const double rate =
        NextShuttleRate(shuttle_rate_, direction, ui_playing_);
    const bool rebuilt = dirty_;
    if (!Ensure()) {
        Report("Preview engine unavailable", true);
        return;
    }
    if (!ui_playing_) {
        StartPlayback(rate);
    } else {
        if (rebuilt) {
            // Re-init mid-shuttle: resume at the current playhead
            // instead of the engine's post-Init t=0.
            engine_->Seek(playhead_);
        }
        // SetRate while playing rebases the wall-clock anchor — the
        // playhead never jumps on a rate flip (engine contract).
        engine_->SetRate(rate);
        engine_->SetPlaying(true);
        SetShuttleRate(rate);
        if (panel_ != nullptr) panel_->SetPlaying(true);
    }
    char label[32];
    std::snprintf(label, sizeof(label), "x%g", rate);
    Report(std::string("Shuttle ") + label);
}

void PreviewBinding::HeldShuttle(double rate) {
    // Fixed-rate shuttle for K+J/K+L held combos — NOT the ladder.
    // The router re-fires the held binding on OS auto-repeat, so the
    // apply path must be idempotent: SetRate while playing just
    // rebases the wall-clock anchor at the same rate.
    const bool rebuilt = dirty_;
    if (!Ensure()) {
        Report("Preview engine unavailable", true);
        return;
    }
    if (!ui_playing_) {
        StartPlayback(rate);
    } else {
        if (rebuilt) {
            engine_->Seek(playhead_);
        }
        engine_->SetRate(rate);
        engine_->SetPlaying(true);
        SetShuttleRate(rate);
        if (panel_ != nullptr) panel_->SetPlaying(true);
    }
}

void PreviewBinding::ShuttleStop() {
    // Premiere K = pause only (Space toggles) — also the shuttle
    // brake; a paused K is a no-op beyond resetting the ladder.
    Stop();
}

void PreviewBinding::Stop() {
    SetShuttleRate(0.0);
    if (ready_) {
        engine_->SetRate(1.0);
        engine_->SetPlaying(false);
    }
    ui_playing_ = false;
    if (ready_) {
        // Still frame at the stop point (old SetTime(.., force) —
        // the paused-seek presents a frame).
        engine_->Seek(playhead_);
    }
    if (panel_ != nullptr) {
        panel_->SetPlaying(false);
        panel_->SetTimecode(playhead_);
        panel_->VideoSurface().SetTime(playhead_);
    }
}

void PreviewBinding::Seek(double seconds) {
    playhead_ =
        std::clamp(seconds, 0.0, std::max(0.0, Duration()));
    if (ready_) engine_->Seek(playhead_);
    if (panel_ != nullptr) {
        panel_->SetTimecode(playhead_);
        panel_->VideoSurface().SetTime(playhead_);
    }
    if (on_playhead_) on_playhead_(playhead_);
}

void PreviewBinding::StepFrame(int direction) {
    const double frame_dt =
        static_cast<double>(fps_den_) / std::max(1, fps_num_);
    Seek(playhead_ + direction * frame_dt);
}

void PreviewBinding::ToStart() { Seek(0.0); }
void PreviewBinding::ToEnd() { Seek(Duration()); }

bool PreviewBinding::Playing() const {
    return ui_playing_;
}

// ---- marshal ----------------------------------------------------------------------

void PreviewBinding::DrainPending() {
    // Swap pending values out under the lock; apply unlocked —
    // applying a frame is a ref move + paint-state update and holding
    // the mutex through it stalls the engine worker.
    Render::scoped_refptr<Render::VideoFrame> frame;
    bool has_frame = false, has_peaks = false;
    float pl = 0.0f, pr = 0.0f;
    double playhead = -1.0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (has_frame_) {
            frame = std::move(pending_frame_);
            pending_frame_.reset();
            has_frame_ = false;
            has_frame = true;
        }
        if (has_peaks_) {
            pl = pending_peak_l_;
            pr = pending_peak_r_;
            has_peaks_ = false;
            has_peaks = true;
        }
        if (pending_playhead_ >= 0.0) {
            playhead = pending_playhead_;
            pending_playhead_ = -1.0;
        }
    }
    // Frame stats -> the monitor's drop stoplight + adaptive badge
    // (Premiere's indicator drains per UI tick like the frame pull).
    if (ready_) {
        const PreviewEngine::FrameStats s = engine_->GetFrameStats();
        last_stats_.rendered = s.rendered;
        last_stats_.dropped = s.dropped;
        last_stats_.last_render_ms = s.last_render_ms;
        last_stats_.effective_scale = s.effective_scale;
        last_stats_.adaptive_level = s.adaptive_level;
        last_stats_.scheduler_dropped = s.scheduler_dropped;
        last_stats_.scheduler_missed = s.scheduler_missed;
        if (panel_ != nullptr) {
            panel_->SetDropStats(s.dropped, s.rendered,
                                 s.last_render_ms, s.adaptive_level,
                                 s.effective_scale);
        }
        // Status-strip mirror ("dropped N") — throttled: the count
        // can grow every tick under sustained overload.
        const auto now = std::chrono::steady_clock::now();
        if (s.dropped > 0 && s.dropped != last_reported_dropped_ &&
            now - last_drop_report_ >= std::chrono::seconds(1)) {
            last_reported_dropped_ = s.dropped;
            last_drop_report_ = now;
            Report("dropped " + std::to_string(s.dropped) +
                   " frame" + (s.dropped == 1 ? "" : "s"));
        }
    }
    if (panel_ == nullptr) {
        if (playhead >= 0.0) {
            playhead_ =
                std::clamp(playhead, 0.0, std::max(0.0, Duration()));
            if (on_playhead_) on_playhead_(playhead_);
        }
        return;
    }
    if (has_frame) panel_->SetVideoFrame(std::move(frame));
    if (has_peaks) panel_->SetPeakLevels(pl, pr);
    if (playhead >= 0.0) {
        playhead_ =
            std::clamp(playhead, 0.0, std::max(0.0, Duration()));
        panel_->SetTimecode(playhead_);
        panel_->VideoSurface().SetTime(playhead_);
        if (on_playhead_) on_playhead_(playhead_);
        if (ready_ && !engine_->Playing() && ui_playing_) {
            // Engine auto-stopped at the end (or at t=0 on a reverse
            // shuttle) — mirror onto the chrome.
            SetShuttleRate(0.0);
            ui_playing_ = false;
            panel_->SetPlaying(false);
        }
    }
}

// ---- pull seams ---------------------------------------------------------

std::pair<float, float> PreviewBinding::PeakLevels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // has_peaks_ is consumed by DrainPending but the slots keep the
    // last-reported values — exactly what a meter pull wants.
    return {pending_peak_l_, pending_peak_r_};
}

bool PreviewBinding::ClipCached(const std::string& clip_id) const {
    return ready_ && engine_ != nullptr &&
           engine_->ClipCachedFrames(clip_id) > 0;
}

}  // namespace XER::VideoEditor::Ui2
