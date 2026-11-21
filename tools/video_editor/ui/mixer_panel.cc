#include "mixer_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

// ---- strip geometry ----
constexpr float kStripWidth = 84.0f;
constexpr float kStripGap = 8.0f;
constexpr float kStripInset = 8.0f;

// ---- fader / meter ranges ----
constexpr float kGainMinDb = -60.0f;   // fader bottom ("-inf")
constexpr float kGainMaxDb = 12.0f;    // fader top
constexpr float kMeterMinDb = -60.0f;
constexpr float kMeterRedDb = -3.0f;   // over-zone starts here
// Ballistics mirror MonitorWidget::SetPeakLevels.
constexpr float kMeterFallDbPerSec = 20.0f;
constexpr double kPeakHoldSeconds = 1.6;
constexpr float kPeakFallDbPerSec = 12.0f;
// Drag pixels for a full -1..1 pan sweep; |db| below this snaps to 0.
constexpr float kPanDragPixels = 40.0f;
constexpr float kGainSnapDb = 0.15f;
constexpr auto kDoubleClickWindow = std::chrono::milliseconds(450);

float ToDb(float linear_peak) {
    if (linear_peak <= 0.0f || !std::isfinite(linear_peak)) {
        return kMeterMinDb;
    }
    return std::max(kMeterMinDb, 20.0f * std::log10(linear_peak));
}

float ClampGain(float db) {
    if (!std::isfinite(db)) return 0.0f;
    return std::clamp(db, kGainMinDb, kGainMaxDb);
}

// Meter palette: the reference's teal program-meter fill (#2dd4a0);
// the over-zone above -3 dB still reads danger. The teal literal is a
// candidate theme token once the token set gains a semantic for it.
Color MeterTeal() { return {0.18f, 0.83f, 0.63f, 1.0f}; }
Color MeterRed() { return {0.93f, 0.26f, 0.22f, 1.0f}; }

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

std::string FormatGain(float db) {
    if (db <= kGainMinDb + 0.05f) return "-inf";
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%+.1f dB", db);
    return buffer;
}

std::string FormatPan(float pan) {
    if (std::fabs(pan) < 0.01f) return "C";
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%s%d",
                  pan < 0.0f ? "L" : "R",
                  static_cast<int>(std::lround(std::fabs(pan) * 100.0f)));
    return buffer;
}

}  // namespace

MixerPanelWidget::MixerPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // strips scroll horizontally inside
}

void MixerPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    hint_ = std::make_shared<Widgets::Label>(
        "drag faders \xc2\xb7 dbl-click resets \xc2\xb7 wheel scrolls",
        true);
    AddChild(hint_);
}

void MixerPanelWidget::RebuildStrips(const Project* project) {
    // Keep the meter/drag state of strips that survive the rebuild
    // (matched by track index; the master strip always survives).
    std::vector<Strip> previous = std::move(strips_);
    strips_.clear();
    if (project != nullptr) {
        int audio_index = 0;
        for (std::size_t i = 0; i < project->tracks.size(); ++i) {
            const Track& track = project->tracks[i];
            if (track.kind != TrackKind::Audio) continue;
            Strip strip;
            strip.track_index = static_cast<int>(i);
            strip.name = track.name.empty()
                             ? "A" + std::to_string(++audio_index)
                             : track.name;
            strip.gain_db = ClampGain(track.gain_db);
            strip.muted = track.muted;
            // Pan lives on the track model — loaded projects and undo
            // must show it. pan_by_track_ only shadows the value while
            // THIS strip's knob is mid-drag (the model write lags the
            // pointer by a frame, same reason SetProject preserves the
            // fader drag).
            const bool pan_drag =
                drag_mode_ == DragMode::Pan && drag_strip_ >= 0 &&
                drag_strip_ < static_cast<int>(previous.size()) &&
                previous[static_cast<std::size_t>(drag_strip_)]
                        .track_index == strip.track_index;
            const auto pan = pan_by_track_.find(strip.track_index);
            strip.pan = pan_drag && pan != pan_by_track_.end()
                            ? pan->second
                            : std::clamp(track.pan, -1.0f, 1.0f);
            // Carry meter peaks over for a strip that kept its index.
            for (const Strip& old : previous) {
                if (!old.master && old.track_index == strip.track_index) {
                    strip.peak_in_l = old.peak_in_l;
                    strip.peak_in_r = old.peak_in_r;
                    strip.meter_l_db = old.meter_l_db;
                    strip.meter_r_db = old.meter_r_db;
                    strip.hold_l_db = old.hold_l_db;
                    strip.hold_r_db = old.hold_r_db;
                    break;
                }
            }
            strips_.push_back(std::move(strip));
        }
    }
    // The master strip is always present (last).
    Strip master;
    master.track_index = -1;
    master.name = "Master";
    master.master = true;
    master.gain_db = ClampGain(master_gain_db_);
    for (const Strip& old : previous) {
        if (old.master) {
            master.peak_in_l = old.peak_in_l;
            master.peak_in_r = old.peak_in_r;
            master.meter_l_db = old.meter_l_db;
            master.meter_r_db = old.meter_r_db;
            master.hold_l_db = old.hold_l_db;
            master.hold_r_db = old.hold_r_db;
            break;
        }
    }
    strips_.push_back(std::move(master));
    if (drag_strip_ >= static_cast<int>(strips_.size())) {
        drag_strip_ = -1;
        drag_mode_ = DragMode::None;
    }
    scroll_x_ = std::clamp(scroll_x_, 0.0f, ScrollRange());

    // Rebuild the mute-chip buttons to match: one child Button per
    // track strip (Destructive while muted), none on the master.
    namespace Widgets = XER::UI::Widgets;
    for (const auto& button : mute_buttons_) {
        if (button) RemoveChild(button);
    }
    mute_buttons_.clear();
    mute_buttons_.reserve(strips_.size());
    for (std::size_t i = 0; i < strips_.size(); ++i) {
        const Strip& s = strips_[i];
        if (s.master) {
            mute_buttons_.push_back(nullptr);
            continue;
        }
        auto button = std::make_shared<Widgets::Button>(
            "M", s.muted ? Widgets::ButtonStyle::Destructive
                         : Widgets::ButtonStyle::Secondary);
        // Panel keys/shortcuts stay on the mixer — the chips take
        // pointer input only.
        button->SetFocusable(false);
        const int track_index = s.track_index;
        button->SetOnClick([this, track_index, button] {
            for (Strip& strip : strips_) {
                if (!strip.master && strip.track_index == track_index) {
                    strip.muted = !strip.muted;
                    button->SetStyle(
                        strip.muted ? Widgets::ButtonStyle::Destructive
                                    : Widgets::ButtonStyle::Secondary);
                    button->SetTooltip(strip.muted ? "Unmute track"
                                                   : "Mute track");
                    if (hooks_.on_track_mute) {
                        hooks_.on_track_mute(strip.track_index,
                                             strip.muted);
                    }
                    Invalidate();
                    break;
                }
            }
        });
        button->SetTooltip(s.muted ? "Unmute track" : "Mute track");
        AddChild(button);
        mute_buttons_.push_back(std::move(button));
    }
}

void MixerPanelWidget::SetProject(const Project* project) {
    // Preserve a mid-drag fader value across the rebuild — the model
    // can lag the pointer by a frame and would otherwise snap the
    // thumb back mid-gesture.
    const bool keep_drag =
        drag_mode_ == DragMode::Fader && drag_strip_ >= 0 &&
        drag_strip_ < static_cast<int>(strips_.size());
    const int drag_track =
        keep_drag ? strips_[static_cast<std::size_t>(drag_strip_)]
                        .track_index
                  : -2;
    const float drag_db =
        keep_drag ? strips_[static_cast<std::size_t>(drag_strip_)]
                        .gain_db
                  : 0.0f;
    RebuildStrips(project);
    if (keep_drag) {
        for (std::size_t i = 0; i < strips_.size(); ++i) {
            if (strips_[i].track_index == drag_track) {
                strips_[i].gain_db = drag_db;
                drag_strip_ = static_cast<int>(i);
                break;
            }
        }
    }
    Invalidate();
}

void MixerPanelWidget::SetPeak(int strip, float l, float r) {
    if (strips_.empty()) return;
    // -1 or the strip count selects the master strip (always last).
    int index = strip;
    if (index < 0 || index >= static_cast<int>(strips_.size())) {
        index = static_cast<int>(strips_.size()) - 1;
    }
    Strip& s = strips_[static_cast<std::size_t>(index)];
    s.peak_in_l = std::isfinite(l) ? std::max(0.0f, l) : 0.0f;
    s.peak_in_r = std::isfinite(r) ? std::max(0.0f, r) : 0.0f;
}

void MixerPanelWidget::SetMasterGainDb(float db) {
    master_gain_db_ = ClampGain(db);
    if (!strips_.empty() && strips_.back().master &&
        drag_mode_ != DragMode::Fader) {
        strips_.back().gain_db = master_gain_db_;
    }
    Invalidate();
}

float MixerPanelWidget::TrackPan(int track_index) const {
    const auto it = pan_by_track_.find(track_index);
    return it == pan_by_track_.end() ? 0.0f : it->second;
}

// ---- geometry ----

Rect MixerPanelWidget::StripRect(int strip) const {
    const auto bounds = GetBounds();
    return {bounds.x + kStripInset +
                static_cast<float>(strip) * (kStripWidth + kStripGap) -
                scroll_x_,
            strips_top_, kStripWidth, strips_bottom_ - strips_top_};
}

Rect MixerPanelWidget::FaderTrackRect(const Rect& strip) const {
    return {strip.x + 18.0f, strip.y + 40.0f, 6.0f,
            strip.height - 40.0f - 46.0f};
}

Rect MixerPanelWidget::PanRect(const Rect& strip) const {
    return {strip.x + 30.0f, strip.y + 8.0f, 24.0f, 22.0f};
}

Rect MixerPanelWidget::MuteRect(const Rect& strip) const {
    return {strip.x + 6.0f, strip.y + 8.0f, 20.0f, 20.0f};
}

Rect MixerPanelWidget::MeterRect(const Rect& strip) const {
    const Rect fader = FaderTrackRect(strip);
    // Bars + a dB-tick label column on the strip's right edge.
    return {strip.Right() - 30.0f, fader.y, 30.0f, fader.height};
}

int MixerPanelWidget::StripAt(const Point& position) const {
    for (std::size_t i = 0; i < strips_.size(); ++i) {
        if (StripRect(static_cast<int>(i)).Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

float MixerPanelWidget::DbFromFaderY(float y, const Rect& track) const {
    const float frac =
        std::clamp((track.y + track.height - y) / track.height, 0.0f,
                   1.0f);
    float db = kGainMinDb + frac * (kGainMaxDb - kGainMinDb);
    if (std::fabs(db) < kGainSnapDb) db = 0.0f;
    return db;
}

float MixerPanelWidget::FaderYFromDb(float db, const Rect& track) const {
    const float frac = std::clamp(
        (db - kGainMinDb) / (kGainMaxDb - kGainMinDb), 0.0f, 1.0f);
    return track.y + track.height - frac * track.height;
}

float MixerPanelWidget::ScrollRange() const {
    const float view_w =
        GetBounds().width - kStripInset * 2.0f;
    const float content = static_cast<float>(strips_.size()) *
                              (kStripWidth + kStripGap) -
                          kStripGap;
    return std::max(0.0f, content - std::max(view_w, 0.0f));
}

void MixerPanelWidget::SetStripGain(int strip, float db) {
    if (strip < 0 || strip >= static_cast<int>(strips_.size())) return;
    Strip& s = strips_[static_cast<std::size_t>(strip)];
    db = ClampGain(db);
    if (db == s.gain_db) return;
    s.gain_db = db;
    if (s.master) {
        master_gain_db_ = db;
        if (hooks_.on_master_gain) hooks_.on_master_gain(db);
    } else if (hooks_.on_track_gain) {
        hooks_.on_track_gain(s.track_index, db);
    }
    Invalidate();
}

void MixerPanelWidget::SetStripPan(int strip, float pan) {
    if (strip < 0 || strip >= static_cast<int>(strips_.size())) return;
    Strip& s = strips_[static_cast<std::size_t>(strip)];
    if (s.master) return;
    pan = std::clamp(std::isfinite(pan) ? pan : 0.0f, -1.0f, 1.0f);
    if (pan == s.pan) return;
    s.pan = pan;
    pan_by_track_[s.track_index] = pan;
    if (hooks_.on_track_pan) hooks_.on_track_pan(s.track_index, pan);
    Invalidate();
}

// ---- drawing ----

void MixerPanelWidget::DrawStrip(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens, int index,
    const Rect& strip) const {
    const Strip& s = strips_[static_cast<std::size_t>(index)];
    const bool hovered = index == hovered_strip_;
    const bool dragging = index == drag_strip_ &&
                          drag_mode_ != DragMode::None;

    renderer.FillRect(strip,
                      hovered || dragging ? tokens.hover : tokens.panel,
                      tokens.radius_small);
    if (dragging) {
        renderer.StrokeRect(strip, WithAlpha(tokens.accent, 0.6f), 1.0f,
                            tokens.radius_small);
    }

    const Color text = s.muted ? tokens.muted : tokens.foreground;
    const XER::UI::Rendering::TextStyle label_style{
        text, tokens.font_small, 500};
    const XER::UI::Rendering::TextStyle dim_style{
        tokens.muted, tokens.font_small, 400};

    // Pan knob (track strips only): ring + indicator swept from top,
    // -1 = hard left .. +1 = hard right.
    if (!s.master) {
        const Rect knob = PanRect(strip);
        const float cx = knob.x + knob.width * 0.5f;
        const float cy = knob.y + knob.height * 0.5f;
        const float r = std::min(knob.width, knob.height) * 0.5f - 1.0f;
        renderer.FillRect({cx - r, cy - r, r * 2.0f, r * 2.0f},
                          tokens.secondary, r);
        renderer.StrokeRect({cx - r, cy - r, r * 2.0f, r * 2.0f},
                            tokens.border, 1.0f, r);
        // 0 = pointing up; pan sweeps +-120 degrees.
        const float angle = s.pan * 2.0944f;   // 120 deg in radians
        const float dx = std::sin(angle);
        const float dy = -std::cos(angle);
        renderer.DrawLine({cx, cy},
                          {cx + dx * (r - 2.0f), cy + dy * (r - 2.0f)},
                          s.pan == 0.0f ? tokens.muted : tokens.accent,
                          1.6f);
        renderer.DrawText(
            FormatPan(s.pan),
            {knob.x - 6.0f, knob.y + knob.height + 1.0f,
             knob.width + 12.0f, 12.0f},
            dim_style);
    } else {
        renderer.DrawText("MASTER",
                          {strip.x + 6.0f, strip.y + 10.0f,
                           strip.width - 12.0f, 14.0f},
                          {tokens.muted, tokens.font_small, 700});
    }

    // Gain fader: recessed track, 0 dB tick, thumb.
    const Rect track = FaderTrackRect(strip);
    renderer.FillRect(track, tokens.input, track.width * 0.5f);
    const float y0 = FaderYFromDb(0.0f, track);
    renderer.DrawLine({track.x - 3.0f, y0}, {track.Right() + 3.0f, y0},
                      WithAlpha(tokens.muted, 0.6f), 1.0f);
    const float thumb_y = FaderYFromDb(s.gain_db, track);
    const Rect thumb{track.x - 6.0f, thumb_y - 4.5f, track.width + 12.0f,
                     9.0f};
    renderer.FillRect(thumb,
                      dragging && drag_mode_ == DragMode::Fader
                          ? tokens.accent
                          : tokens.foreground,
                      tokens.radius_small);
    // Fader grip line.
    renderer.DrawLine({thumb.x + 3.0f, thumb_y},
                      {thumb.Right() - 3.0f, thumb_y},
                      WithAlpha(tokens.background, 0.8f), 1.0f);

    // Peak meters: two teal bars with a dB-tick column (0/-6/-12/-24),
    // peak-hold ticks and an "S" (stereo bus) label under the pair —
    // the reference audio-meters look. Master strip only — the host
    // feeds SetPeak(-1, ...) exclusively (the mix bus), so per-track
    // bars would sit permanently dead.
    if (s.master) {
        const Rect meter = MeterRect(strip);
        constexpr float kBarW = 5.0f;
        constexpr float kBarGap = 7.0f;
        // The bottom 12px carry the "S" channel label, not bar.
        const float bars_h = meter.height - 12.0f;
        const auto y_for_db = [&](float db) {
            const float u = std::clamp(
                (db - kMeterMinDb) / (0.0f - kMeterMinDb), 0.0f, 1.0f);
            return meter.y + bars_h - u * bars_h;
        };
        const auto draw_bar = [&](float x, float level_db,
                                  float hold_db) {
            renderer.FillRect({x, meter.y, kBarW, bars_h},
                              tokens.input, 1.0f);
            const float level_y = y_for_db(std::min(level_db, 0.0f));
            if (level_y < meter.y + bars_h) {
                renderer.FillRect(
                    {x, level_y, kBarW,
                     meter.y + bars_h - level_y},
                    MeterTeal(), 0.0f);
                // Over-zone: the portion above -3 dB reads danger.
                if (level_db > kMeterRedDb) {
                    const float over_y = y_for_db(kMeterRedDb);
                    renderer.FillRect(
                        {x, level_y, kBarW,
                         std::max(0.0f, over_y - level_y)},
                        MeterRed(), 0.0f);
                }
            }
            if (hold_db > kMeterMinDb + 0.5f) {
                const float y_peak = y_for_db(std::min(hold_db, 0.0f));
                renderer.DrawLine({x, y_peak}, {x + kBarW, y_peak},
                                  hold_db > kMeterRedDb
                                      ? MeterRed()
                                      : Color{1, 1, 1, 0.9f},
                                  1.5f);
            }
        };
        draw_bar(meter.x, s.meter_l_db, s.hold_l_db);
        draw_bar(meter.x + kBarGap, s.meter_r_db, s.hold_r_db);

        // dB tick column right of the bars: hairline ticks + tiny
        // mono labels at the reference's 0/-6/-12/-24 marks.
        const XER::UI::Rendering::TextStyle tick_style{
            tokens.muted, tokens.font_small - 4.0f, 400,
            /*monospace=*/true};
        const float tick_x = meter.x + kBarGap + kBarW + 3.0f;
        const float label_w = meter.Right() - tick_x - 2.0f;
        for (const float db : {0.0f, -6.0f, -12.0f, -24.0f}) {
            const float ty = y_for_db(db);
            renderer.DrawLine({tick_x - 3.0f, ty}, {tick_x, ty},
                              WithAlpha(tokens.muted, 0.6f), 1.0f);
            char label[8];
            std::snprintf(label, sizeof(label), "%d",
                          static_cast<int>(db));
            const auto size = renderer.MeasureText(label, tick_style);
            renderer.DrawText(
                label,
                {tick_x + label_w - std::min(size.width, label_w),
                 ty - size.height * 0.5f,
                 std::min(size.width, label_w), size.height},
                tick_style);
        }
        // "S" under the stereo pair — channel label per the
        // reference's audio-meters panel (track strips carry the "M"
        // mute chip already).
        renderer.DrawText(
            "S",
            {meter.x - 2.0f, meter.y + bars_h,
             kBarGap + kBarW + 4.0f, 12.0f},
            XER::UI::Rendering::TextStyle{tokens.muted,
                                          tokens.font_small, 600});
    }

    // Gain readout + name (muted strips dim everything; the name row
    // trades its label for a red "muted" tag).
    renderer.DrawText(
        FormatGain(s.gain_db),
        {strip.x + 4.0f, strip.y + strip.height - 42.0f,
         strip.width - 8.0f, 14.0f},
        s.muted ? dim_style
                : XER::UI::Rendering::TextStyle{tokens.accent,
                                                tokens.font_small, 600});
    if (s.muted) {
        renderer.DrawText("muted",
                          {strip.x + 4.0f,
                           strip.y + strip.height - 24.0f,
                           strip.width - 8.0f, 16.0f},
                          {tokens.danger, tokens.font_small, 600});
    } else {
        renderer.DrawText(s.name,
                          {strip.x + 4.0f,
                           strip.y + strip.height - 24.0f,
                           strip.width - 8.0f, 16.0f},
                          label_style);
    }
}

void MixerPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    // Collapsible-style panel header — the shared VeIcons chrome.
    {
        const float pad = tokens.spacing_medium;
        VeIcons::DrawSectionHeader(
            renderer, tokens,
            {bounds.x + pad, bounds.y + pad * 0.6f,
             bounds.width - pad * 2.0f, VeTheme::kRowHeight},
            "Mixer", false);
    }
    if (strips_top_ <= 0.0f) return;   // not yet laid out

    if (strips_.size() <= 1) {
        // Only the master strip exists — keep the hint clear of it.
        renderer.DrawText("add an audio track to mix",
                          {bounds.x + kStripInset + kStripWidth +
                               kStripGap + 4.0f,
                           strips_top_ + 10.0f, bounds.width - 24.0f,
                           20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
    }

    const Rect viewport{bounds.x + 2.0f, strips_top_,
                        bounds.width - 4.0f,
                        strips_bottom_ - strips_top_};
    renderer.PushClip(viewport);
    for (std::size_t i = 0; i < strips_.size(); ++i) {
        const Rect strip = StripRect(static_cast<int>(i));
        if (strip.x + strip.width < viewport.x ||
            strip.x > viewport.x + viewport.width) {
            continue;
        }
        DrawStrip(renderer, tokens, static_cast<int>(i), strip);
    }
    renderer.PopClip();
}

void MixerPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    // The header row is drawn chrome (see Render); the hint keeps its
    // right-aligned band on the same row.
    const bool wide = bounds.width > 430.0f;
    hint_->SetVisible(wide);
    hint_->Layout({bounds.x + bounds.width - pad - 280.0f,
                   bounds.y + pad * 0.6f + 3.0f, 280.0f, 18.0f},
                  tokens);
    strips_top_ = bounds.y + 34.0f + tokens.spacing_small;
    strips_bottom_ = bounds.y + bounds.height - 10.0f;
    scroll_x_ = std::clamp(scroll_x_, 0.0f, ScrollRange());

    // Mute chips ride their strips — hidden once the strip scrolls
    // fully out of the viewport.
    const Rect viewport{bounds.x + 2.0f, strips_top_,
                        bounds.width - 4.0f,
                        strips_bottom_ - strips_top_};
    for (std::size_t i = 0;
         i < strips_.size() && i < mute_buttons_.size(); ++i) {
        const auto& button = mute_buttons_[i];
        if (!button) continue;
        const Rect chip = MuteRect(StripRect(static_cast<int>(i)));
        button->SetVisible(chip.x + chip.width >= viewport.x &&
                           chip.x <= viewport.x + viewport.width);
        button->Layout(chip, tokens);
    }
}

bool MixerPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    namespace Widgets = XER::UI::Widgets;
    switch (event.type) {
        case Widgets::Event::Type::MouseWheel: {
            const float range = ScrollRange();
            if (range <= 0.0f) return false;
            scroll_x_ = std::clamp(
                scroll_x_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
            return true;
        }
        case Widgets::Event::Type::MouseDown: {
            if (event.mouse_button != Widgets::MouseButton::Left) {
                return false;
            }
            const int strip = StripAt(event.position);
            if (strip < 0) return false;
            const Rect rect = StripRect(strip);
            const Strip& s = strips_[static_cast<std::size_t>(strip)];
            if (FaderTrackRect(rect)
                    .Union(Rect{rect.x + 10.0f,
                                FaderTrackRect(rect).y, 22.0f,
                                FaderTrackRect(rect).height})
                    .Contains(event.position)) {
                drag_strip_ = strip;
                drag_mode_ = DragMode::Fader;
                drag_start_db_ = s.gain_db;
                SetStripGain(strip, DbFromFaderY(event.position.y,
                                                 FaderTrackRect(rect)));
                return true;
            }
            if (!s.master &&
                PanRect(rect).Contains(event.position)) {
                drag_strip_ = strip;
                drag_mode_ = DragMode::Pan;
                pan_drag_start_ = s.pan;
                pan_drag_x_ = event.position.x;
                return true;
            }
            return false;
        }
        case Widgets::Event::Type::MouseMove: {
            if (drag_strip_ >= 0) {
                const Rect rect = StripRect(drag_strip_);
                if (drag_mode_ == DragMode::Fader) {
                    SetStripGain(
                        drag_strip_,
                        DbFromFaderY(event.position.y,
                                     FaderTrackRect(rect)));
                } else if (drag_mode_ == DragMode::Pan) {
                    SetStripPan(drag_strip_,
                                pan_drag_start_ +
                                    (event.position.x - pan_drag_x_) /
                                        kPanDragPixels);
                }
                return true;
            }
            const int strip = StripAt(event.position);
            if (strip != hovered_strip_) {
                hovered_strip_ = strip;
                Invalidate();
            }
            return false;
        }
        case Widgets::Event::Type::MouseUp: {
            if (event.mouse_button == Widgets::MouseButton::Left &&
                drag_strip_ >= 0) {
                const Strip& s =
                    strips_[static_cast<std::size_t>(drag_strip_)];
                just_dragged_ =
                    (drag_mode_ == DragMode::Pan &&
                     std::fabs(s.pan - pan_drag_start_) > 0.005f) ||
                    (drag_mode_ == DragMode::Fader &&
                     std::fabs(s.gain_db - drag_start_db_) > 0.01f);
                drag_strip_ = -1;
                drag_mode_ = DragMode::None;
                Invalidate();
                return true;
            }
            return false;
        }
        case Widgets::Event::Type::Click: {
            if (just_dragged_) {
                just_dragged_ = false;
                last_click_strip_ = -1;
                return true;
            }
            const int strip = StripAt(event.position);
            if (strip < 0) return false;
            const Rect rect = StripRect(strip);
            Strip& s = strips_[static_cast<std::size_t>(strip)];
            // The mute chip is a Button child now — it owns its click.
            // Double-click on fader / pan knob resets the control.
            const auto now = std::chrono::steady_clock::now();
            DragMode mode = DragMode::None;
            if (FaderTrackRect(rect)
                    .Union(Rect{rect.x + 10.0f,
                                FaderTrackRect(rect).y, 22.0f,
                                FaderTrackRect(rect).height})
                    .Contains(event.position)) {
                mode = DragMode::Fader;
            } else if (!s.master &&
                       PanRect(rect).Contains(event.position)) {
                mode = DragMode::Pan;
            }
            if (mode != DragMode::None) {
                const bool double_click =
                    strip == last_click_strip_ && mode == last_click_mode_ &&
                    (now - last_click_time_) < kDoubleClickWindow;
                last_click_strip_ = strip;
                last_click_mode_ = mode;
                last_click_time_ = now;
                if (double_click) {
                    if (mode == DragMode::Fader) {
                        SetStripGain(strip, 0.0f);
                    } else {
                        SetStripPan(strip, 0.0f);
                    }
                }
                return true;
            }
            return true;   // consume clicks anywhere on a strip
        }
        default:
            return false;
    }
}

void MixerPanelWidget::Tick(double dt_seconds) {
    // Instant attack, ~20 dB/s release, 1.6 s peak-hold then decay —
    // the same ballistics the monitor meters use.
    const float fall =
        kMeterFallDbPerSec * static_cast<float>(dt_seconds);
    const float peak_fall =
        kPeakFallDbPerSec * static_cast<float>(dt_seconds);
    bool dirty = false;
    for (Strip& s : strips_) {
        const float in_l = ToDb(s.peak_in_l);
        const float in_r = ToDb(s.peak_in_r);
        const float next_l =
            in_l >= s.meter_l_db ? in_l : s.meter_l_db - fall;
        const float next_r =
            in_r >= s.meter_r_db ? in_r : s.meter_r_db - fall;
        if (std::fabs(next_l - s.meter_l_db) > 0.01f ||
            std::fabs(next_r - s.meter_r_db) > 0.01f) {
            s.meter_l_db = next_l;
            s.meter_r_db = next_r;
            dirty = true;
        }
        const auto hold = [&](float meter, float& hold_db,
                              double& hold_secs) {
            if (meter >= hold_db) {
                hold_db = meter;
                hold_secs = 0.0;
                dirty = true;
            } else {
                hold_secs += dt_seconds;
                if (hold_secs > kPeakHoldSeconds &&
                    hold_db > kMeterMinDb) {
                    hold_db = std::max(kMeterMinDb, hold_db - peak_fall);
                    dirty = true;
                }
            }
        };
        hold(s.meter_l_db, s.hold_l_db, s.hold_l_secs);
        hold(s.meter_r_db, s.hold_r_db, s.hold_r_secs);
        // Raw peaks decay so a stalled feed doesn't pin the bars.
        s.peak_in_l *= 0.92f;
        s.peak_in_r *= 0.92f;
    }
    if (dirty) Invalidate();
}

}  // namespace XER::VideoEditor::Ui
