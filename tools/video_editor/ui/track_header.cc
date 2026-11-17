#include "track_header.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/slider.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

Color WithAlpha(Color color, float alpha) {
    color.a = alpha;
    return color;
}

// Track gain range mirrored on the mixer (audio-fader convention).
constexpr float kGainMinDb = -60.0f;
constexpr float kGainMaxDb = 12.0f;

constexpr float kChipSize = 16.0f;
constexpr float kChipStep = 20.0f;
constexpr float kChipTop = 7.0f;
constexpr float kPadX = 8.0f;

const char* kChipLabels[] = {"M", "H", "L"};
const char kChipIds[] = {'m', 'h', 'l'};

std::string FormatGainDb(float db) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%+.1f dB", db);
    return buffer;
}

}  // namespace

TrackHeader::TrackHeader(Hooks hooks) : hooks_(std::move(hooks)) {
    SetClipsChildren(false);
}

void TrackHeader::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    gain_slider_ = std::make_shared<Widgets::Slider>(kGainMinDb,
                                                   kGainMaxDb, 0.0f);
    gain_slider_->SetTooltip("Track gain (dB)");
    gain_slider_->SetOnValueChanged([this](float db) {
        if (syncing_gain_) return;
        gain_db_ = db;
        if (hooks_.on_gain) hooks_.on_gain(db);
    });
    AddChild(gain_slider_);
}

void TrackHeader::SetTrack(const Track& track, int index) {
    EnsureChildren();
    index_ = index;
    kind_ = track.kind;
    name_ = track.name;
    muted_ = track.muted;
    hidden_ = track.hidden;
    locked_ = track.locked;
    gain_db_ = track.gain_db;
    syncing_gain_ = true;
    gain_slider_->SetValue(
        std::clamp(track.gain_db, kGainMinDb, kGainMaxDb));
    syncing_gain_ = false;
    Invalidate();
}

Layout::Rect TrackHeader::BadgeRect() const {
    const auto bounds = GetBounds();
    return {bounds.x + kPadX, bounds.y + 6.0f, 26.0f, 18.0f};
}

Layout::Rect TrackHeader::ChipRect(int chip) const {
    const auto bounds = GetBounds();
    return {bounds.x + bounds.width - kPadX - (3 - chip) * kChipStep,
            bounds.y + kChipTop, kChipSize, kChipSize};
}

int TrackHeader::ChipAt(const Layout::Point& position) const {
    for (int chip = 0; chip < 3; ++chip) {
        if (ChipRect(chip).Contains(position)) return chip;
    }
    return -1;
}

void TrackHeader::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();

    // Card — same chrome as the timeline's inline header cells.
    renderer.FillRect(bounds, tokens.background, tokens.radius_medium);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_medium);

    // Index badge: kind letter + 1-based index ("V1", "A2").
    const auto badge = BadgeRect();
    char badge_text[8];
    std::snprintf(badge_text, sizeof(badge_text), "%c%d",
                  kind_ == TrackKind::Video ? 'V' : 'A', index_ + 1);
    renderer.FillRect(badge, tokens.secondary, tokens.radius_small);
    renderer.DrawText(badge_text, badge,
                      {tokens.muted, tokens.font_small, 700});

    // Track name next to the badge; locked tracks dim like the
    // timeline's own headers.
    const std::string shown_name =
        name_.empty() ? "Track " + std::to_string(index_ + 1) : name_;
    const float name_right =
        ChipRect(0).x - 6.0f;   // leave room for the three chips
    renderer.DrawText(
        shown_name,
        {badge.x + badge.width + 8.0f, bounds.y + 7.0f,
         std::max(0.0f, name_right - badge.x - badge.width - 8.0f),
         18.0f},
        {locked_ ? tokens.muted : tokens.foreground, tokens.font_small,
         600});

    // M/H/L chips — same fill scheme as TimelineWidget's header
    // buttons: accent for mute/hide, danger for lock.
    const bool active[3] = {muted_, hidden_, locked_};
    for (int chip = 0; chip < 3; ++chip) {
        const auto rect = ChipRect(chip);
        const Color fill = active[chip]
            ? (chip == 2 ? tokens.danger : tokens.accent)
            : WithAlpha(tokens.muted, 0.25f);
        const Color text = active[chip] ? tokens.accent_foreground
                                        : tokens.muted;
        renderer.FillRect(rect, fill, 3.0f);
        renderer.StrokeRect(rect, tokens.border, 1.0f, 3.0f);
        renderer.DrawText(kChipLabels[chip],
                          {rect.x, rect.y + 1.0f, rect.width,
                           rect.height},
                          {text, tokens.font_small, 700});
    }

    // Gain readout, right of the slider.
    renderer.DrawText(
        FormatGainDb(gain_db_),
        {bounds.x + bounds.width - 58.0f, bounds.y + 34.0f, 52.0f,
         16.0f},
        {tokens.muted, tokens.font_small, 400});
}

void TrackHeader::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    // Bottom row: slider takes the width left of the dB readout.
    gain_slider_->Layout(
        {bounds.x + kPadX, bounds.y + 30.0f,
         std::max(0.0f, bounds.width - kPadX * 2.0f - 56.0f), 22.0f},
        tokens);
}

bool TrackHeader::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        const int chip = ChipAt(event.position);
        if (chip >= 0) {
            const bool current =
                chip == 0 ? muted_ : chip == 1 ? hidden_ : locked_;
            if (hooks_.on_toggle) {
                hooks_.on_toggle(kChipIds[chip], !current);
            }
            return true;
        }
    }
    return false;
}

void TrackHeader::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
