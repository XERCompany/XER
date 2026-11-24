// render/frame_scheduler.cc — see frame_scheduler.h for the model.
#include "frame_scheduler.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace XER::VideoEditor::Render {

// ---------------------------------------------------------------------
// WallClockTimeSource
// ---------------------------------------------------------------------

WallClockTimeSource::WallClockTimeSource() {
    const auto epoch = std::chrono::steady_clock::now();
    now_fn_ = [epoch] {
        return std::chrono::duration<double>(
                   std::chrono::steady_clock::now() - epoch)
            .count();
    };
}

WallClockTimeSource::WallClockTimeSource(NowFn now_fn)
    : now_fn_(std::move(now_fn)) {}

void WallClockTimeSource::Start(double media_time) {
    anchor_media_ = media_time;
    anchor_wall_ = now_fn_();
    running_ = true;
}

void WallClockTimeSource::Pause() {
    anchor_media_ = MediaTimeSeconds();
    anchor_wall_ = now_fn_();
    running_ = false;
}

void WallClockTimeSource::SetPlaybackRate(double rate) {
    // Freeze the current estimate into the anchor so a rate flip never
    // jumps the playhead — same rule as PreviewEngine::SetRate.
    const double now = now_fn_();
    anchor_media_ = running_
                        ? anchor_media_ + (now - anchor_wall_) * rate_
                        : anchor_media_;
    anchor_wall_ = now;
    rate_ = rate;
}

void WallClockTimeSource::SetMediaTime(double t) {
    anchor_media_ = t;
    anchor_wall_ = now_fn_();
}

double WallClockTimeSource::MediaTimeSeconds() const {
    if (!running_) {
        return anchor_media_;
    }
    return anchor_media_ + (now_fn_() - anchor_wall_) * rate_;
}

// ---------------------------------------------------------------------
// AudioClockTimeSource
// ---------------------------------------------------------------------

AudioClockTimeSource::AudioClockTimeSource(int sample_rate)
    : sample_rate_(sample_rate > 0 ? sample_rate : 48000) {}

void AudioClockTimeSource::WroteAudioSamples(std::int64_t samples) {
    if (samples > 0) {
        samples_written_.fetch_add(samples, std::memory_order_relaxed);
    }
}

void AudioClockTimeSource::SetBaseTime(double media_time) {
    // Chromium AudioClock::SetBaseTimestamp: rebasing also zeroes the
    // accumulator — otherwise already-written samples would be double
    // counted against the new base.
    samples_written_.store(0, std::memory_order_relaxed);
    base_ = media_time;
}

void AudioClockTimeSource::SetPlaybackRate(double rate) {
    rate_ = rate;
}

void AudioClockTimeSource::Start(double media_time) {
    SetBaseTime(media_time);
    paused_ = false;
}

void AudioClockTimeSource::Pause() {
    paused_at_ = MediaTimeSeconds();
    paused_ = true;
}

double AudioClockTimeSource::MediaTimeSeconds() const {
    if (paused_) {
        return paused_at_;
    }
    return base_ + static_cast<double>(
                       samples_written_.load(
                           std::memory_order_relaxed)) /
                       static_cast<double>(sample_rate_);
}

// ---------------------------------------------------------------------
// FrameScheduler
// ---------------------------------------------------------------------

FrameScheduler::FrameScheduler(TimeSource* media_clock)
    : clock_(media_clock) {}

void FrameScheduler::SetDisplayInterval(double seconds) {
    if (seconds > 0.0 && std::isfinite(seconds)) {
        vsync_interval_ = seconds;
    }
}

void FrameScheduler::SetMediaFrameDuration(double seconds) {
    if (seconds > 0.0 && std::isfinite(seconds)) {
        frame_duration_ = seconds;
    }
}

void FrameScheduler::Reset() {
    pending_valid_ = false;
    presented_since_tick_ = false;
    last_presented_valid_ = false;
    // sequence_/counters_ intentionally survive — they're monotonic
    // diagnostics, like the engine's FramesPresented across seeks.
}

FrameTick FrameScheduler::BeginFrame(double wall_now_seconds) {
    FrameTick tick;
    tick.sequence = ++sequence_;
    ++counters_.ticks;
    tick.frame_time_wall = wall_now_seconds;

    // Next display boundary on the vsync grid (BeginFrameSource's
    // phase-aligned frame_time — the frame a render triggered now
    // would actually land on).
    tick.display_time_wall =
        std::floor(wall_now_seconds / vsync_interval_ + 1e-9) *
            vsync_interval_ +
        vsync_interval_;

    const double rate = clock_->playback_rate();
    const double media_now = clock_->MediaTimeSeconds();
    // The media-time interval this frame will occupy on screen: media
    // time at the target vsync through one display interval of
    // playback. Reverse rate flips the window (deadline_min <= max).
    const double media_at_display =
        media_now + (tick.display_time_wall - wall_now_seconds) * rate;
    const double span = vsync_interval_ * rate;
    if (rate >= 0.0) {
        tick.deadline_min = media_at_display;
        tick.deadline_max = media_at_display + span;
    } else {
        tick.deadline_min = media_at_display + span;
        tick.deadline_max = media_at_display;
    }

    // Missed deadline: the previous tick's window fully expired in
    // media time without a present landing in it. Half a media frame
    // of slack keeps borderline races out of the counter.
    if (pending_valid_ && !presented_since_tick_ &&
        media_now > pending_deadline_max_ + frame_duration_ * 0.5) {
        ++counters_.missed_deadlines;
    }

    // Drop budget (VRA RemoveExpiredFrames analogue): media-frame
    // slots the deadline window has moved past since the last
    // presented frame, minus the step the current rate legitimately
    // skips (|rate|=2 renders every other frame on purpose).
    if (last_presented_valid_) {
        const double step =
            std::max(1.0, std::round(std::fabs(rate)));
        const double gap_slots =
            (tick.deadline_min - last_presented_media_) /
            frame_duration_;
        tick.drop_budget = std::clamp(
            static_cast<int>(std::floor(gap_slots - step + 0.5)), 0,
            static_cast<int>(kMaxDropPerTick));
    }

    pending_valid_ = true;
    pending_deadline_max_ = tick.deadline_max;
    presented_since_tick_ = false;
    return tick;
}

void FrameScheduler::OnFramePresented(double media_pts_seconds) {
    presented_since_tick_ = true;
    ++counters_.presented;
    if (last_presented_valid_) {
        const double step = std::max(
            1.0, std::round(std::fabs(clock_->playback_rate())));
        const auto slots = static_cast<std::int64_t>(std::llround(
            (media_pts_seconds - last_presented_media_) /
            frame_duration_));
        if (slots > step) {
            counters_.dropped += static_cast<std::uint64_t>(
                slots - static_cast<std::int64_t>(step));
        }
    }
    last_presented_media_ = media_pts_seconds;
    last_presented_valid_ = true;
}

double FrameScheduler::preferred_render_interval() const {
    const double rate =
        std::max(std::fabs(clock_->playback_rate()), 1e-9);
    return frame_duration_ / rate;
}

}  // namespace XER::VideoEditor::Render
