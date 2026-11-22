#include "level_meter.h"

#include <algorithm>
#include <cmath>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

Theme::Color WithAlpha(Theme::Color color, float alpha) {
    color.a *= alpha;
    return color;
}

// Peak ballistics (audiocn): near-instant rise, steady linear fall.
constexpr float kPeakAttackS = 0.015f;
constexpr float kPeakFallDbPerS = 16.0f;
// VU ballistics: ~300 ms to 99% — exponential smoothing time constant.
constexpr float kVuTauS = 0.300f;
// Sticky clip: fresh clips paint solid for this long before the latched
// indicator dims (the latch itself persists until click-to-clear).
constexpr float kClipHoldS = 1.5f;
// Peak-hold tick decays at this rate once its hold time expires.
constexpr float kHoldFallDbPerS = 12.0f;

constexpr float kClipBox = 10.0f;
constexpr float kClipGap = 3.0f;
constexpr float kChannelGap = 3.0f;

} // namespace

LevelMeter::LevelMeter() {
    SetFocusable(true);
    channels_.resize(static_cast<std::size_t>(channel_count_));
}

void LevelMeter::SetLevels(float peak_db, float rms_db) {
    FeedChannel(0, peak_db, rms_db);
}

void LevelMeter::SetLevels(int channel, float peak_db, float rms_db) {
    FeedChannel(channel, peak_db, rms_db);
}

void LevelMeter::SetLevels(const std::vector<ChannelLevels>& levels) {
    const int n = static_cast<int>(levels.size());
    if (n != channel_count_) {
        SetChannelCount(std::max(1, n));
    }
    for (int i = 0; i < channel_count_ && i < n; ++i) {
        FeedChannel(i, levels[static_cast<std::size_t>(i)].peak_db,
                    levels[static_cast<std::size_t>(i)].rms_db);
    }
}

void LevelMeter::SetFrameSource(FrameSource source) {
    frame_source_ = std::move(source);
}

void LevelMeter::SetChannelCount(int count) {
    channel_count_ = std::clamp(count, 1, 16);
    channels_.resize(static_cast<std::size_t>(channel_count_));
    Invalidate();
}

void LevelMeter::SetBallistics(Ballistics ballistics) {
    ballistics_ = ballistics;
}

void LevelMeter::SetTaper(Taper taper) {
    taper_ = taper;
    Invalidate();
}

void LevelMeter::SetDbRange(float min_db, float max_db) {
    if (max_db <= min_db) {
        max_db = min_db + 1.0f;
    }
    min_db_ = min_db;
    max_db_ = max_db;
    Invalidate();
}

void LevelMeter::SetClipThreshold(float db) {
    clip_db_ = db;
}

void LevelMeter::SetZones(std::vector<Zone> zones) {
    zones_ = std::move(zones);
    Invalidate();
}

void LevelMeter::SetPeakHold(float seconds) {
    peak_hold_s_ = seconds;
}

void LevelMeter::SetOrientation(Orientation orientation) {
    orientation_ = orientation;
    Invalidate();
}

void LevelMeter::SetOnClip(std::function<void(int channel)> callback) {
    on_clip_ = std::move(callback);
}

bool LevelMeter::IsClipped() const {
    for (const auto& ch : channels_) {
        if (ch.clipped) {
            return true;
        }
    }
    return false;
}

void LevelMeter::ClearClip() {
    for (auto& ch : channels_) {
        ch.clipped = false;
        ch.clip_time_s = 0.0f;
    }
    Invalidate();
}

void LevelMeter::FeedChannel(int channel, float peak_db, float rms_db) {
    if (channel < 0 || channel >= channel_count_) {
        return;
    }
    auto& ch = channels_[static_cast<std::size_t>(channel)];
    ch.peak_db = peak_db;
    ch.rms_db = rms_db;
    if (peak_db >= clip_db_) {
        // Fresh latch: fires OnClip once; the latch itself is sticky
        // (DAW convention) and only clears via ClearClip()/click.
        if (!ch.clipped && on_clip_) {
            on_clip_(channel);
        }
        ch.clipped = true;
        ch.clip_time_s = kClipHoldS;
    }
}

float LevelMeter::DbToFraction(float db) const {
    const float span = max_db_ - min_db_;
    float t = (std::clamp(db, min_db_, max_db_) - min_db_) / span;
    // Audio taper: quiet dB are compressed into the lower share of the
    // track so the loud end gets most of the travel.
    return taper_ == Taper::Audio ? t * t : t;
}

Theme::Color LevelMeter::ZoneColor(ZoneRole role,
                                   const Theme::Tokens& tokens) const {
    switch (role) {
    case ZoneRole::Low:     return tokens.muted;
    case ZoneRole::Optimum: return tokens.track_audio;
    case ZoneRole::High:    return tokens.warning_chip;
    case ZoneRole::Clip:    return tokens.danger;
    }
    return tokens.accent;
}

void LevelMeter::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    const float dt = static_cast<float>(dt_seconds);

    // Pull mode: the engine owns the level loop; we just sample it here.
    if (frame_source_) {
        const auto frame = frame_source_();
        const int n = std::min(channel_count_,
                               static_cast<int>(frame.size()));
        for (int i = 0; i < n; ++i) {
            const auto& f = frame[static_cast<std::size_t>(i)];
            FeedChannel(i, f.peak_db, f.rms_db);
        }
    }

    const bool instant =
        ballistics_ == Ballistics::Instant || Motion::ReducedMotion();
    bool moving = false;
    for (auto& ch : channels_) {
        for (int pass = 0; pass < 2; ++pass) {
            const float input = pass == 0 ? ch.peak_db : ch.rms_db;
            float& show = pass == 0 ? ch.show_peak_db : ch.show_rms_db;
            const float before = show;
            if (instant) {
                show = input;
            } else if (ballistics_ == Ballistics::Vu) {
                show += (input - show) * std::min(1.0f, dt / kVuTauS);
            } else {  // Peak
                if (input >= show) {
                    // ~15 ms rise: exponential approach, nearly instant.
                    show += (input - show) * std::min(1.0f, dt / kPeakAttackS);
                } else {
                    show = std::max(input, show - kPeakFallDbPerS * dt);
                }
            }
            if (before != show) {
                moving = true;
            }
        }

        // Peak-hold tick: grab fresh peaks, linger, then decay.
        if (peak_hold_s_ > 0.0f) {
            if (ch.show_peak_db >= ch.hold_db) {
                ch.hold_db = ch.show_peak_db;
                ch.hold_time_s = peak_hold_s_;
                moving = true;
            } else if (ch.hold_time_s > 0.0f) {
                ch.hold_time_s -= dt;
            } else if (ch.hold_db > ch.show_peak_db) {
                ch.hold_db = std::max(ch.show_peak_db,
                                      ch.hold_db - kHoldFallDbPerS * dt);
                moving = true;
            }
        } else {
            ch.hold_db = ch.show_peak_db;
        }

        if (ch.clip_time_s > 0.0f) {
            ch.clip_time_s -= dt;
            moving = true;
        }
    }
    if (moving) {
        Invalidate();
    }
}

void LevelMeter::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void LevelMeter::RenderChannel(Rendering::Renderer& renderer,
                               const Theme::Tokens& tokens,
                               int channel, const Layout::Rect& lane,
                               const Layout::Rect& clip_box,
                               const std::vector<Zone>& zones) {
    const auto& ch = channels_[static_cast<std::size_t>(channel)];
    const bool enabled = IsEnabled();
    const bool vertical = orientation_ == Orientation::Vertical;

    // Recessed well for the lane.
    renderer.FillRect(lane, tokens.input, 1.0f);

    const float peak_f = DbToFraction(ch.show_peak_db);
    const float rms_f = DbToFraction(ch.show_rms_db);

    // Layered bars: peak fills the whole lane at half strength; the RMS
    // bar sits inset inside it fully lit (audiocn LevelMeterBar
    // measure="peak"|"rms" two-bar layering). Per-zone fills keep each
    // segment its own theme color.
    for (int measure = 0; measure < 2; ++measure) {
        const float level_f = measure == 0 ? peak_f : rms_f;
        const float alpha = measure == 0 ? 0.5f : 0.95f;
        Layout::Rect bar = lane;
        if (measure == 1) {
            if (vertical) {
                const float inset = bar.width * 0.25f;
                bar.x += inset;
                bar.width -= inset * 2.0f;
            } else {
                const float inset = bar.height * 0.25f;
                bar.y += inset;
                bar.height -= inset * 2.0f;
            }
        }
        for (const auto& zone : zones) {
            const float z_lo = DbToFraction(zone.from_db);
            const float z_hi = DbToFraction(zone.to_db);
            // Faint segment backdrop only on the peak pass.
            const Theme::Color base = enabled
                ? ZoneColor(zone.role, tokens) : tokens.muted;
            if (measure == 0) {
                Layout::Rect seg = lane;
                if (vertical) {
                    const float y0 = lane.y + (1.0f - z_hi) * lane.height;
                    const float y1 = lane.y + (1.0f - z_lo) * lane.height;
                    seg.y = y0;
                    seg.height = std::max(0.0f, y1 - y0);
                } else {
                    const float x0 = lane.x + z_lo * lane.width;
                    const float x1 = lane.x + z_hi * lane.width;
                    seg.x = x0;
                    seg.width = std::max(0.0f, x1 - x0);
                }
                renderer.FillRect(seg, WithAlpha(base, 0.14f), 0.0f);
            }
            const float lit = std::min(level_f, z_hi);
            if (lit <= z_lo) {
                continue;
            }
            Layout::Rect seg = bar;
            if (vertical) {
                const float y0 = bar.y + (1.0f - lit) * bar.height;
                const float y1 = bar.y + (1.0f - z_lo) * bar.height;
                seg.y = y0;
                seg.height = std::max(0.0f, y1 - y0);
            } else {
                const float x0 = bar.x + z_lo * bar.width;
                const float x1 = bar.x + lit * bar.width;
                seg.x = x0;
                seg.width = std::max(0.0f, x1 - x0);
            }
            renderer.FillRect(seg, WithAlpha(base, alpha), 0.0f);
        }
    }

    // Peak-hold tick.
    if (peak_hold_s_ > 0.0f && ch.hold_db > min_db_) {
        const float hf = DbToFraction(ch.hold_db);
        const Theme::Color hold_color =
            enabled ? tokens.accent_foreground : tokens.muted;
        if (vertical) {
            const float y = lane.y + (1.0f - hf) * lane.height;
            renderer.FillRect({lane.x, y - 1.0f, lane.width, 2.0f},
                              hold_color, 0.0f);
        } else {
            const float x = lane.x + hf * lane.width;
            renderer.FillRect({x - 1.0f, lane.y, 2.0f, lane.height},
                              hold_color, 0.0f);
        }
    }

    // Clip indicator: solid danger while a clip is fresh (≤ ~1.5 s),
    // dimmed but still latched afterwards, empty outline at rest.
    if (ch.clipped) {
        const Theme::Color clip_color =
            enabled ? tokens.danger : tokens.muted;
        renderer.FillRect(clip_box,
                          WithAlpha(clip_color,
                                    ch.clip_time_s > 0.0f ? 1.0f : 0.45f),
                          2.0f);
    } else {
        renderer.StrokeRect(clip_box, WithAlpha(tokens.border, 0.8f),
                            1.0f, 2.0f);
    }
}

void LevelMeter::Render(Rendering::Renderer& renderer,
                        const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width < 6.0f || bounds.height < 12.0f) {
        return;
    }
    const bool vertical = orientation_ == Orientation::Vertical;
    const int count = channel_count_;

    // Empty SetZones() restores the default 3-segment ladder:
    // optimum to -9 dB, hot to -3 dB, clip red on top.
    std::vector<Zone> default_zones;
    if (zones_.empty()) {
        default_zones = {
            {min_db_, -9.0f, ZoneRole::Optimum},
            {-9.0f, -3.0f, ZoneRole::High},
            {-3.0f, max_db_, ZoneRole::Clip},
        };
    }
    const auto& zones = zones_.empty() ? default_zones : zones_;

    // Hover wash (interactive: click clears the clip latch).
    if (IsHovered() && IsEnabled()) {
        renderer.FillRect(bounds, WithAlpha(tokens.hover, 0.35f), 1.0f);
    }

    if (vertical) {
        const float lane_w = std::max(
            2.0f, (bounds.width - kChannelGap * (count - 1)) / count);
        const float lane_h = bounds.height - kClipBox - kClipGap;
        for (int i = 0; i < count; ++i) {
            const float x = bounds.x + i * (lane_w + kChannelGap);
            RenderChannel(renderer, tokens, i,
                          {x, bounds.y + kClipBox + kClipGap, lane_w, lane_h},
                          {x, bounds.y, lane_w, kClipBox}, zones);
        }
    } else {
        const float lane_h = std::max(
            2.0f, (bounds.height - kChannelGap * (count - 1)) / count);
        const float lane_w = bounds.width - kClipBox - kClipGap;
        for (int i = 0; i < count; ++i) {
            const float y = bounds.y + i * (lane_h + kChannelGap);
            RenderChannel(renderer, tokens, i,
                          {bounds.x, y, lane_w, lane_h},
                          {bounds.x + bounds.width - kClipBox, y,
                           kClipBox, lane_h}, zones);
        }
    }

    RenderFocusRing(renderer, tokens, 2.0f);
}

bool LevelMeter::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    // DAW convention: clicking the meter clears the sticky clip latch.
    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        if (IsClipped()) {
            ClearClip();
        }
        return true;
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        if (IsClipped()) {
            ClearClip();
        }
        return true;
    }
    return false;
}

Layout::Size LevelMeter::GetMinSize() const {
    return orientation_ == Orientation::Vertical
        ? Layout::Size{8.0f * channel_count_, 32.0f}
        : Layout::Size{48.0f, 6.0f * channel_count_};
}

Layout::Size LevelMeter::GetPreferredSize() const {
    return orientation_ == Orientation::Vertical
        ? Layout::Size{4.0f + 10.0f * channel_count_, 120.0f}
        : Layout::Size{160.0f, 4.0f + 10.0f * channel_count_};
}

Layout::Size LevelMeter::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
