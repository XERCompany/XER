#include "timeline_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "frame.h"
#include "generated_source.h"
#include "js_fx.h"
#include "media_catalog.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/context_menu.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

// ---- layout metrics (VeTheme 4px grid) ----
constexpr float kHeaderWidth = 172.0f;    // track-header column
constexpr float kMarkerLaneHeight = 18.0f;
constexpr float kRulerHeight = 28.0f;
constexpr float kWorkBarHeight = 10.0f;   // band at the ruler's top edge
constexpr float kTrackHeight = 64.0f;
constexpr float kTrackHeightExpanded = 116.0f;  // header-chevron open row
constexpr float kTrackGap = 3.0f;
constexpr float kClipInset = 4.0f;
constexpr float kEdgeGrabPx = 7.0f;
constexpr float kHeaderButtonSize = 18.0f;
constexpr float kHeaderButtonStep = 22.0f;
constexpr float kClipLabelHeight = 17.0f; // name strip at a clip's top
constexpr float kClipRadius = VeTheme::kRadiusMd;
constexpr float kKindBarWidth = 3.0f;     // colored bar on header's left
// Corner-toolbar ghost buttons inside the header corner box.
constexpr float kToolButtonSize = 20.0f;
constexpr float kToolButtonStep = 26.0f;
constexpr int kToolButtonCount = 4;       // magnet, razor, marker, fx
constexpr double kMinZoom = 8.0;
constexpr double kMaxZoom = 600.0;
constexpr std::chrono::milliseconds kDoubleClick{450};
constexpr std::size_t kMaxWaveRows = 24;   // downsampled-waveform LRU
constexpr std::size_t kMaxPosters = 160;   // poster-frame LRU
constexpr std::size_t kMaxPeaks = 32;      // decoded AudioPeaks (MBs each)
constexpr std::size_t kMaxMediaDurations = 512;  // per-path doubles
constexpr std::size_t kMaxMissingKeys = 1024;    // negative-result sets
constexpr double kPosterQuantize = 0.25;   // poster cache quantum (s)
constexpr double kFallbackSampleRate = 48000.0;  // when bounds are unknown

// ---- timeline palette literals ----
// Reference-Premiere colors with no Theme::Tokens equivalent yet —
// candidates for new tokens once the theme pass lands:
//   accent_cyan      (playhead, ruler marks, snap-on, selection)
//   clip_video_fill  (desaturated blue clip body)
//   clip_audio_fill  (dark teal clip body)
//   waveform_teal    (bright in-clip waveform stroke)
//   badge_video/audio(track-id chips on the headers)
//   fx_amber         ("fx" effect chips)
constexpr Color kAccentCyan{0.13f, 0.83f, 0.93f, 1.0f};      // #22d3ee
constexpr Color kClipVideoFill{0.28f, 0.42f, 0.55f, 1.0f};   // desat. blue
constexpr Color kClipVideoFillAlt{0.33f, 0.41f, 0.60f, 1.0f};
constexpr Color kClipAudioFill{0.10f, 0.35f, 0.33f, 1.0f};   // dark teal
constexpr Color kClipAudioFillAlt{0.10f, 0.32f, 0.40f, 1.0f};
constexpr Color kWaveformTeal{0.37f, 0.86f, 0.71f, 1.0f};    // in-clip wave
constexpr Color kBadgeVideo{0.21f, 0.47f, 0.80f, 1.0f};      // "V1" chip
constexpr Color kBadgeAudio{0.07f, 0.55f, 0.51f, 1.0f};      // "A1" chip
constexpr Color kFxAmber{0.96f, 0.62f, 0.05f, 1.0f};         // "fx" chip

Color WithAlpha(Color color, float alpha) {
    color.a = alpha;
    return color;
}

Color MarkerColor(const Marker& marker) {
    return Color{marker.color[0] / 255.0f, marker.color[1] / 255.0f,
                 marker.color[2] / 255.0f, marker.color[3] / 255.0f};
}

// DrawText is left-aligned — chips/badges center their glyph by
// measuring first.
void DrawCenteredText(XER::UI::Rendering::Renderer& renderer,
                      std::string_view text, Rect box,
                      XER::UI::Rendering::TextStyle style) {
    const auto size = renderer.MeasureText(text, style);
    const float tx =
        box.x + std::max(0.0f, (box.width - size.width) * 0.5f);
    renderer.DrawText(
        text, Rect{tx, box.y, box.width - (tx - box.x), box.height},
        style);
}

std::string Basename(const std::string& path) {
    const std::size_t pos = path.find_last_of("/\\");
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// Name-strip label: the user-set Clip::name beats everything;
// generated sources (empty source_path) get a kind/text label so
// mattes, bars and titles stay identifiable; media keeps the source
// basename.
std::string ClipLabel(const Clip& clip) {
    if (!clip.name.empty()) return clip.name;
    if (!clip.source_path.empty()) return Basename(clip.source_path);
    if (clip.is_adjustment) return "Adjustment Layer";
    switch (clip.source.kind) {
        case SourceKind::ColorMatte:
            return "Color Matte";
        case SourceKind::ColorBars:
            return "Color Bars";
        case SourceKind::Text: {
            const ClipTextStyle style =
                ParseClipTextStyle(clip.source.param);
            const std::size_t nl = style.text.find_first_of("\r\n");
            const std::string text = style.text.substr(0, nl);
            return text.empty() ? "Title" : text;
        }
        default:
            return "Clip";
    }
}

std::string FormatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "0:00";
    const int minutes = static_cast<int>(seconds / 60.0);
    const int whole = static_cast<int>(std::fmod(seconds, 60.0));
    const int millis = static_cast<int>(
        std::round((seconds - std::floor(seconds)) * 1000.0));
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d.%03d", minutes, whole,
                  millis == 1000 ? 999 : millis);
    return buffer;
}

// Clip body color for a track: reference-style desaturated blue for
// video lanes, dark teal for audio, with a per-track hue flip so
// neighbouring same-kind tracks stay distinguishable.
Color ClipBaseFor(const Track& track, int index) {
    const bool alt = (static_cast<std::size_t>(index) % 2u) != 0u;
    if (track.kind == TrackKind::Video) {
        return alt ? kClipVideoFillAlt : kClipVideoFill;
    }
    return alt ? kClipAudioFillAlt : kClipAudioFill;
}

// Track-kind badge accent (header kind bar, badge chip, row wash).
Color KindColor(bool video_track) {
    return video_track ? kBadgeVideo : kBadgeAudio;
}

// Header affordance ids (see TimelineWidget::HeaderHit):
//   0 = mute (audio), 1 = eye/hidden (video), 2 = lock,
//   3 = expand chevron, 4 = solo (audio), 5 = record arm (audio).
// Kind gate mirroring ShowContextMenu's track menu: mute/solo/arm are
// audio-track concepts, the hidden/eye a video-track one; lock and the
// expand chevron apply to both.
bool HeaderButtonApplies(const Track& track, int button) {
    switch (button) {
        case 0:                                          // M = muted
        case 4:                                          // S = solo
        case 5:                                          // mic = arm
            return track.kind == TrackKind::Audio;
        case 1: return track.kind == TrackKind::Video;   // eye = hidden
        default: return true;                            // lock, chevron
    }
}

// Ordered bottom-strip button ids per kind — slot order is the
// left-to-right paint order in RenderHeaderButtons.
int HeaderButtonIdFor(const Track& track, int slot) {
    if (track.kind == TrackKind::Audio) {
        static constexpr int kAudio[] = {0, 4, 5, 2};   // M S mic lock
        return kAudio[slot];
    }
    static constexpr int kVideo[] = {1, 2};             // eye lock
    return kVideo[slot];
}

int HeaderButtonCount(const Track& track) {
    return track.kind == TrackKind::Audio ? 4 : 2;
}

Rect HeaderButtonRect(const Rect& label_rect, int slot, int count) {
    // Chips sit on their own strip at the header's bottom edge —
    // 8px right margin / 6px bottom, 4px gaps via kHeaderButtonStep.
    return Rect{label_rect.x + label_rect.width - VeTheme::kSpaceSm -
                    (count - slot) * kHeaderButtonStep,
                label_rect.y + label_rect.height -
                    kHeaderButtonSize - 6.0f,
                kHeaderButtonSize, kHeaderButtonSize};
}

// Expand chevron — top-right corner of the header card.
Rect HeaderChevronRect(const Rect& label_rect) {
    return Rect{label_rect.x + label_rect.width - 22.0f,
                label_rect.y + 6.0f, 16.0f, 16.0f};
}

// Diamond outline (marker glyph) — four DrawLine segments, no polygon
// primitive in the renderer.
void DrawDiamond(XER::UI::Rendering::Renderer& renderer, Point center,
                 float radius, Color color, float width) {
    renderer.DrawLine({center.x, center.y - radius},
                      {center.x + radius, center.y}, color, width);
    renderer.DrawLine({center.x + radius, center.y},
                      {center.x, center.y + radius}, color, width);
    renderer.DrawLine({center.x, center.y + radius},
                      {center.x - radius, center.y}, color, width);
    renderer.DrawLine({center.x - radius, center.y},
                      {center.x, center.y - radius}, color, width);
}

// ---- small icon glyphs ----
// The shared SVG icon pack has no timeline glyphs (lock/eye/mic/
// magnet/razor), so the header chips and corner toolbar draw them as
// line art — same affordance language, no new assets.

// Eye: almond outline (two-segment lids) + pupil dot; `slashed` draws
// the "hidden" diagonal strike.
void DrawEyeIcon(XER::UI::Rendering::Renderer& renderer, Rect box,
                 Color color, bool slashed) {
    const float cx = box.x + box.width * 0.5f;
    const float mid = box.y + box.height * 0.5f;
    const float lx = box.x + 1.5f, rx = box.x + box.width - 1.5f;
    const float ty = box.y + 2.0f, by = box.y + box.height - 2.0f;
    renderer.DrawLine({lx, mid}, {cx, ty}, color, 1.3f);
    renderer.DrawLine({cx, ty}, {rx, mid}, color, 1.3f);
    renderer.DrawLine({lx, mid}, {cx, by}, color, 1.3f);
    renderer.DrawLine({cx, by}, {rx, mid}, color, 1.3f);
    renderer.FillRect({cx - 1.6f, mid - 1.6f, 3.2f, 3.2f}, color, 1.6f);
    if (slashed) {
        renderer.DrawLine({lx + 0.5f, by - 0.5f}, {rx - 0.5f, ty + 0.5f},
                          color, 1.4f);
    }
}

// Padlock: squared shackle + filled body.
void DrawLockIcon(XER::UI::Rendering::Renderer& renderer, Rect box,
                  Color color) {
    const float cx = box.x + box.width * 0.5f;
    const float top = box.y + 1.5f;
    renderer.DrawLine({cx - 3.0f, top + 4.0f}, {cx - 3.0f, top},
                      color, 1.4f);
    renderer.DrawLine({cx - 3.0f, top}, {cx + 3.0f, top}, color, 1.4f);
    renderer.DrawLine({cx + 3.0f, top}, {cx + 3.0f, top + 4.0f},
                      color, 1.4f);
    renderer.FillRect({cx - 5.0f, top + 4.5f, 10.0f, 7.5f}, color, 2.0f);
}

// Studio mic: capsule + cradle + stem + base (audio record-arm).
void DrawMicIcon(XER::UI::Rendering::Renderer& renderer, Rect box,
                 Color color) {
    const float cx = box.x + box.width * 0.5f;
    const float top = box.y + 1.0f;
    renderer.FillRect({cx - 2.6f, top, 5.2f, 8.4f}, color, 2.6f);
    // Cradle under the capsule.
    renderer.DrawLine({cx - 4.6f, top + 7.0f}, {cx - 4.6f, top + 9.6f},
                      color, 1.3f);
    renderer.DrawLine({cx - 4.6f, top + 9.6f}, {cx + 4.6f, top + 9.6f},
                      color, 1.3f);
    renderer.DrawLine({cx + 4.6f, top + 9.6f}, {cx + 4.6f, top + 7.0f},
                      color, 1.3f);
    // Stem + base.
    renderer.DrawLine({cx, top + 9.6f}, {cx, top + 13.0f}, color, 1.3f);
    renderer.DrawLine({cx - 3.4f, top + 13.0f}, {cx + 3.4f, top + 13.0f},
                      color, 1.3f);
}

// Chevron: "v" collapsed -> expanded row affordance; "^" when open.
void DrawChevronIcon(XER::UI::Rendering::Renderer& renderer, Rect box,
                     Color color, bool expanded) {
    const float cx = box.x + box.width * 0.5f;
    const float cy = box.y + box.height * 0.5f;
    const float d = expanded ? 2.4f : -2.4f;
    renderer.DrawLine({cx - 4.0f, cy - d}, {cx, cy + d}, color, 1.5f);
    renderer.DrawLine({cx, cy + d}, {cx + 4.0f, cy - d}, color, 1.5f);
}

// Horseshoe magnet: squared arch with bright pole tips (snap toggle).
void DrawMagnetIcon(XER::UI::Rendering::Renderer& renderer, Rect box,
                    Color color) {
    const float lx = box.x + 3.5f, rx = box.x + box.width - 3.5f;
    const float top = box.y + 3.0f, bot = box.y + box.height - 2.0f;
    renderer.DrawLine({lx, bot}, {lx, top}, color, 1.6f);
    renderer.DrawLine({lx, top}, {rx, top}, color, 1.6f);
    renderer.DrawLine({rx, top}, {rx, bot}, color, 1.6f);
    // Pole tips — slightly brighter so the glyph reads as a magnet.
    const Color tip = WithAlpha(color, std::min(1.0f, color.a + 0.2f));
    renderer.FillRect({lx - 1.6f, bot - 4.0f, 3.2f, 4.0f}, tip, 0.8f);
    renderer.FillRect({rx - 1.6f, bot - 4.0f, 3.2f, 4.0f}, tip, 0.8f);
}

// Razor blade: skewed parallelogram with a centre slot line.
void DrawRazorIcon(XER::UI::Rendering::Renderer& renderer, Rect box,
                   Color color) {
    const float x = box.x, y = box.y;
    const Point a{x + 2.5f, y + 13.0f}, b{x + 9.0f, y + 13.0f},
        c{x + 13.5f, y + 4.5f}, d{x + 7.0f, y + 4.5f};
    renderer.DrawLine(a, b, color, 1.4f);
    renderer.DrawLine(b, c, color, 1.4f);
    renderer.DrawLine(c, d, color, 1.4f);
    renderer.DrawLine(d, a, color, 1.4f);
    renderer.DrawLine({x + 7.5f, y + 10.5f}, {x + 10.0f, y + 7.0f},
                      color, 1.2f);
}

}  // namespace

TimelineWidget::TimelineWidget(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);
    // One instance per tool — the active id picks the receiver of
    // pointer events (SetTool never allocates on the hot path).
    static constexpr ToolId kAllTools[] = {
        ToolId::Selection, ToolId::Razor,     ToolId::RippleEdit,
        ToolId::RollingEdit, ToolId::Slip,    ToolId::Slide,
        ToolId::RateStretch, ToolId::Hand,    ToolId::Zoom,
    };
    for (ToolId id : kAllTools) {
        tools_[id] = MakeEditTool(id);
    }
}

void TimelineWidget::SetProject(const Project* project) {
    project_ = project;
    // The project object may have been rebuilt by a commit — drop any
    // stale tool previews keyed by old clip ids.
    clip_previews_.clear();
    Invalidate();
}

void TimelineWidget::SetPlayhead(double seconds) {
    playhead_ = std::max(0.0, seconds);
    if (project_ != nullptr) {
        const double duration = std::max(project_->duration(), 1.0);
        const double visible_start = scroll_seconds_;
        const double visible_end =
            scroll_seconds_ + timeline_area_.width / px_per_second_;
        if (playhead_ < visible_start) {
            scroll_seconds_ = playhead_;
        } else if (playhead_ > visible_end - 0.2) {
            scroll_seconds_ =
                std::max(0.0, playhead_ - duration * 0.1);
        }
    }
    Invalidate();
}

void TimelineWidget::SetSelectedClip(const std::string& clip_id) {
    selected_clip_id_ = clip_id;
    Invalidate();
}

void TimelineWidget::SetSnapEnabled(bool enabled) {
    snap_enabled_ = enabled;
    Invalidate();
}

void TimelineWidget::SetZoom(double px_per_second) {
    px_per_second_ = std::clamp(px_per_second, kMinZoom, kMaxZoom);
    Invalidate();
}

void TimelineWidget::SetTool(ToolId tool) {
    if (tool == active_tool_) return;
    if (EditTool* current = ActiveToolImpl()) {
        current->Cancel(*this);
    }
    active_tool_ = tool;
    if (EditTool* next = ActiveToolImpl()) {
        SetStatus(std::string("Tool: ") + next->Name() + " — " +
                  next->CursorHint());
    }
    Invalidate();
}

const char* TimelineWidget::ActiveToolName() const {
    const EditTool* tool = ActiveToolImpl();
    return tool != nullptr ? tool->Name() : "?";
}

const char* TimelineWidget::ActiveToolCursorHint() const {
    const EditTool* tool = ActiveToolImpl();
    return tool != nullptr ? tool->CursorHint() : "";
}

EditTool* TimelineWidget::ActiveToolImpl() const {
    const auto it = tools_.find(active_tool_);
    return it != tools_.end() ? it->second.get() : nullptr;
}

double TimelineWidget::TimeAtDropPoint(
    XER::UI::Layout::Point point) const {
    return TimeAtX(point.x);
}

int TimelineWidget::TrackAtDropPoint(
    XER::UI::Layout::Point point) const {
    return TrackAtY(point.y);
}

void TimelineWidget::SetExternalDropHover(bool active) {
    if (external_drop_hover_ == active) return;
    external_drop_hover_ = active;
    Invalidate();
}

double TimelineWidget::TimeAtX(float x) const {
    const float local = x - (timeline_area_.x + kHeaderWidth);
    return std::max(0.0, scroll_seconds_ + local / px_per_second_);
}

float TimelineWidget::XAtTime(double t) const {
    return timeline_area_.x + kHeaderWidth +
           static_cast<float>((t - scroll_seconds_) * px_per_second_);
}

float TimelineWidget::TrackRowHeight(int index) const {
    return expanded_tracks_.count(index) != 0u ? kTrackHeightExpanded
                                             : kTrackHeight;
}

int TimelineWidget::TrackAtY(float y) const {
    if (project_ == nullptr) return -1;
    const float row_top =
        timeline_area_.y + kMarkerLaneHeight + kRulerHeight;
    // TrackRect() scrolls rows up by track_scroll_, so a y in screen
    // space maps to scrolled content space by adding it back.
    const float local = y - row_top + track_scroll_;
    if (local < 0.0f) return -1;
    // Rows have per-track heights (expand chevrons) — walk the stack.
    float cursor = 0.0f;
    for (std::size_t i = 0; i < project_->tracks.size(); ++i) {
        const float h = TrackRowHeight(static_cast<int>(i));
        if (local < cursor + h) {
            return local - cursor <= h ? static_cast<int>(i) : -1;
        }
        cursor += h + kTrackGap;
        if (local < cursor) return -1;   // inside the gap
    }
    return -1;
}

Rect TimelineWidget::TrackRect(int index) const {
    float y = timeline_area_.y + kMarkerLaneHeight + kRulerHeight -
              track_scroll_;
    for (int i = 0; i < index && i <
            static_cast<int>(project_ != nullptr
                                 ? project_->tracks.size() : 0);
         ++i) {
        y += TrackRowHeight(i) + kTrackGap;
    }
    return Rect{timeline_area_.x + kHeaderWidth, y,
                std::max(0.0f, timeline_area_.width - kHeaderWidth),
                TrackRowHeight(index)};
}

// Corner-toolbar ghost buttons inside the header-column corner box:
// 0 = magnet (snap), 1 = razor tool, 2 = add marker, 3 = fx badge.
Rect TimelineWidget::ToolbarButtonRect(int index) const {
    return Rect{timeline_area_.x + VeTheme::kSpaceMd +
                    static_cast<float>(index) * kToolButtonStep,
                timeline_area_.y + 24.0f, kToolButtonSize,
                kToolButtonSize};
}

int TimelineWidget::ToolbarButtonAt(Point p) const {
    const float corner_bottom =
        timeline_area_.y + kMarkerLaneHeight + kRulerHeight;
    if (p.x >= timeline_area_.x + kHeaderWidth ||
        p.y >= corner_bottom) {
        return -1;
    }
    for (int i = 0; i < kToolButtonCount; ++i) {
        if (ToolbarButtonRect(i).Contains(p)) return i;
    }
    return -1;
}

Rect TimelineWidget::HeaderRect(int index) const {
    const Rect row = TrackRect(index);
    return Rect{timeline_area_.x + VeTheme::kSpaceXs, row.y,
                kHeaderWidth - VeTheme::kSpaceXs * 2.0f, row.height};
}

Rect TimelineWidget::RealClipRect(const Clip& clip) const {
    const Rect track = TrackRect(clip.track);
    const float x0 = XAtTime(clip.timeline_start);
    const float x1 = XAtTime(ClipEndSeconds(clip));
    return Rect{x0, track.y + kClipInset,
                std::max(3.0f, x1 - x0),
                track.height - kClipInset * 2.0f};
}

Rect TimelineWidget::ClipRect(const Clip& clip) const {
    const auto it = clip_previews_.find(clip.id);
    if (it != clip_previews_.end()) {
        const Rect track = TrackRect(clip.track);
        const float x0 = XAtTime(it->second.first);
        const float x1 = XAtTime(it->second.second);
        return Rect{x0, track.y + kClipInset,
                    std::max(3.0f, x1 - x0),
                    track.height - kClipInset * 2.0f};
    }
    return RealClipRect(clip);
}

TimelineWidget::ClipHit TimelineWidget::HitClip(Point p) const {
    if (project_ == nullptr ||
        p.x < timeline_area_.x + kHeaderWidth) {
        return {};
    }
    for (auto it = project_->clips.rbegin(); it != project_->clips.rend();
         ++it) {
        const Clip& clip = *it;
        const Rect rect = ClipRect(clip);
        if (!rect.Contains(p)) continue;
        if (p.x - rect.x <= kEdgeGrabPx) return {&clip, ClipPart::EdgeIn};
        if (rect.x + rect.width - p.x <= kEdgeGrabPx) {
            return {&clip, ClipPart::EdgeOut};
        }
        return {&clip, ClipPart::Body};
    }
    return {};
}

TimelineWidget::HeaderHit TimelineWidget::HitHeaderButton(
    Point p) const {
    if (project_ == nullptr ||
        p.x >= timeline_area_.x + kHeaderWidth) {
        return {};
    }
    const float ruler_bottom =
        timeline_area_.y + kMarkerLaneHeight + kRulerHeight;
    for (std::size_t i = 0; i < project_->tracks.size(); ++i) {
        const Rect label = HeaderRect(static_cast<int>(i));
        // Scrolled-off / partially-hidden headers aren't clickable.
        if (label.y < ruler_bottom || p.y < label.y ||
            p.y >= label.y + label.height) {
            continue;
        }
        const Track& track = project_->tracks[i];
        if (HeaderChevronRect(label).Contains(p)) {
            return HeaderHit{static_cast<int>(i), 3};
        }
        const int count = HeaderButtonCount(track);
        for (int slot = 0; slot < count; ++slot) {
            const int button = HeaderButtonIdFor(track, slot);
            if (!HeaderButtonApplies(track, button)) continue;
            if (HeaderButtonRect(label, slot, count).Contains(p)) {
                return HeaderHit{static_cast<int>(i), button};
            }
        }
        return {};
    }
    return {};
}

const Marker* TimelineWidget::MarkerAtX(float x) const {
    if (project_ == nullptr) return nullptr;
    const Marker* best = nullptr;
    float best_dist = 7.0f;
    for (const Marker& marker : project_->markers) {
        const float dist = std::fabs(XAtTime(marker.time) - x);
        if (dist <= best_dist) {
            best = &marker;
            best_dist = dist;
        }
    }
    return best;
}

int TimelineWidget::TopZoneAt(Point p) const {
    // Only called inside the ruler band (below the marker lane).
    const float band_top = timeline_area_.y + kMarkerLaneHeight;
    if (project_ == nullptr || p.y >= band_top + kWorkBarHeight) {
        return 0;
    }
    const double in =
        work_drag_ != WorkDrag::None ? work_drag_in_ : project_->work_in;
    const double out = work_drag_ != WorkDrag::None ? work_drag_out_
                                                    : project_->work_out;
    if (out <= in) return 0;   // whole-project work area: no handles
    const float xi = XAtTime(in);
    const float xo = XAtTime(out);
    if (std::fabs(p.x - xi) <= 6.0f) return 1;
    if (std::fabs(p.x - xo) <= 6.0f) return 2;
    if (p.x > xi && p.x < xo) return 3;
    return 0;
}

ToolContext TimelineWidget::MakeContext(
    const XER::UI::Widgets::Event& event) const {
    ToolContext ctx;
    ctx.project = project_;
    ctx.position = event.position;
    ctx.time = TimeAtX(event.position.x);
    ctx.track = TrackAtY(event.position.y);
    ctx.playhead = playhead_;
    ctx.px_per_second = px_per_second_;
    ctx.button = event.mouse_button;
    ctx.is_ctrl = event.is_ctrl;
    ctx.is_shift = event.is_shift;
    ctx.is_alt = event.is_alt;
    const ClipHit hit = HitClip(event.position);
    ctx.clip = hit.clip;
    ctx.clip_part = hit.part;
    if (hit.clip != nullptr && project_ != nullptr) {
        constexpr double kEps = 1e-4;
        if (hit.part == ClipPart::EdgeOut) {
            const double end = ClipEndSeconds(*hit.clip);
            const Clip* next =
                NextClipOnTrack(*project_, hit.clip->track, end);
            if (next != nullptr &&
                std::fabs(next->timeline_start - end) <= kEps) {
                ctx.roll_from = hit.clip;
                ctx.roll_to = next;
            }
        } else if (hit.part == ClipPart::EdgeIn) {
            const Clip* prev = PrevClipOnTrack(
                *project_, hit.clip->track, hit.clip->timeline_start);
            if (prev != nullptr &&
                std::fabs(ClipEndSeconds(*prev) -
                          hit.clip->timeline_start) <= kEps) {
                ctx.roll_from = prev;
                ctx.roll_to = hit.clip;
            }
        }
    }
    return ctx;
}

void TimelineWidget::SetStatus(const std::string& text, bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---- media caches ----

namespace {

// FIFO-tracked insert into a bounded miss set — evicts the oldest key
// once `lru` exceeds `cap` so per-session misses stay bounded.
void TrackMissing(std::unordered_set<std::string>& missing,
                  std::deque<std::string>& lru, const std::string& key,
                  std::size_t cap) {
    missing.insert(key);
    lru.push_back(key);
    while (lru.size() > cap) {
        missing.erase(lru.front());
        lru.pop_front();
    }
}

}  // namespace

const AudioPeaks* TimelineWidget::PeaksFor(const std::string& path) {
    if (!hooks_.get_audio_peaks || path.empty()) return nullptr;
    if (peaks_missing_.count(path) != 0u) return nullptr;
    const auto it = peaks_cache_.find(path);
    if (it != peaks_cache_.end()) return it->second.get();
    auto fresh = std::make_shared<AudioPeaks>();
    switch (hooks_.get_audio_peaks(path, fresh.get())) {
        case MediaLookup::kReady:
            // Validate the AudioPeaks contract before caching — a
            // corrupt/partial payload (data shorter than
            // channels*buckets*2 min/max floats) would make WaveFor
            // read out of bounds. Same guard waveform_panel.cc uses.
            // Division form: a forged giant `buckets` cannot wrap the
            // product back under data.size().
            if (fresh->buckets > 0 && fresh->channels > 0 &&
                fresh->data.size() /
                            (2u * static_cast<std::size_t>(
                                      fresh->channels)) >=
                    fresh->buckets) {
                peaks_cache_[path] = fresh;
                peaks_lru_.push_back(path);
                while (peaks_lru_.size() > kMaxPeaks) {
                    peaks_cache_.erase(peaks_lru_.front());
                    peaks_lru_.pop_front();
                }
                return fresh.get();
            }
            // unusable payload = miss
            TrackMissing(peaks_missing_, peaks_missing_lru_, path,
                         kMaxMissingKeys);
            return nullptr;
        case MediaLookup::kPending:
            return nullptr;   // worker decoding — re-asked next frame
        case MediaLookup::kFailed:
            // cached miss — no re-probe
            TrackMissing(peaks_missing_, peaks_missing_lru_, path,
                         kMaxMissingKeys);
            return nullptr;
    }
    return nullptr;
}

void TimelineWidget::NotifyMediaChanged() {
    // Only failed/zero probes are evicted — a positive duration is
    // stable for the session. Dropping misses here keeps the failure
    // cache between notifications (no per-frame re-probe of dead
    // media) while letting a late-arriving probe finally stick.
    for (auto it = media_duration_cache_.begin();
         it != media_duration_cache_.end();) {
        if (it->second <= 0.0) {
            duration_lru_.erase(
                std::remove(duration_lru_.begin(), duration_lru_.end(),
                            it->first),
                duration_lru_.end());
            it = media_duration_cache_.erase(it);
        } else {
            ++it;
        }
    }
}

double TimelineWidget::MediaDurationFor(const std::string& path) {
    if (!hooks_.get_media_bounds || path.empty()) return 0.0;
    const auto it = media_duration_cache_.find(path);
    if (it != media_duration_cache_.end()) return it->second;
    double duration = 0.0;
    MediaBounds bounds;
    if (hooks_.get_media_bounds(path, &bounds) &&
        bounds.duration_seconds > 0.0) {
        duration = bounds.duration_seconds;
    }
    // Failures ARE cached (a missing file must not re-probe every
    // paint) but evicted by NotifyMediaChanged when the media
    // pipeline lands fresh data — until then the waveform keeps its
    // 48 kHz-bucket estimate.
    media_duration_cache_[path] = duration;
    duration_lru_.push_back(path);
    while (duration_lru_.size() > kMaxMediaDurations) {
        media_duration_cache_.erase(duration_lru_.front());
        duration_lru_.pop_front();
    }
    return duration;
}

const TimelineWidget::WaveRow* TimelineWidget::WaveFor(
    const std::string& path, int columns) {
    const AudioPeaks* peaks = PeaksFor(path);
    if (peaks == nullptr || columns <= 0) return nullptr;
    // AudioPeaks carries no sample rate: the whole-media span comes
    // from the media-bounds hook; the 48 kHz fallback keeps the
    // waveform approximately aligned when bounds are unavailable.
    double span = MediaDurationFor(path);
    if (span <= 0.0) {
        span = static_cast<double>(peaks->buckets) *
               peaks->samples_per_peak / kFallbackSampleRate;
    }
    const std::string key =
        path + "@" + std::to_string(columns);
    const auto it = wave_cache_.find(key);
    if (it != wave_cache_.end() &&
        std::fabs(it->second.span_seconds - span) < 1e-6) {
        return &it->second;
    }
    WaveRow* slot;
    if (it != wave_cache_.end()) {
        // Rebuild in place (media duration became known after the
        // first estimate) — the LRU position is kept, no duplicate.
        slot = &it->second;
    } else {
        wave_lru_.push_back(key);
        while (wave_lru_.size() > kMaxWaveRows) {
            wave_cache_.erase(wave_lru_.front());
            wave_lru_.pop_front();
        }
        slot = &wave_cache_[key];
    }
    WaveRow row;
    row.span_seconds = span;
    row.columns = columns;
    row.minmax.assign(static_cast<std::size_t>(columns) * 2u, 0.0f);
    const double sec_per_bucket =
        span / static_cast<double>(peaks->buckets);
    const auto& data = peaks->data;
    const std::size_t buckets = peaks->buckets;
    const int channels = peaks->channels;
    for (int c = 0; c < columns; ++c) {
        const double t0 = span * c / columns;
        const double t1 = span * (c + 1) / columns;
        std::size_t b0 = static_cast<std::size_t>(std::max(
            0.0, std::floor(t0 / sec_per_bucket)));
        std::size_t b1 = static_cast<std::size_t>(std::max(
            0.0, std::ceil(t1 / sec_per_bucket)));
        b0 = std::min(b0, buckets - 1);
        b1 = std::min(std::max(b1, b0 + 1), buckets);
        float mn = 1.0f, mx = -1.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const std::size_t base =
                static_cast<std::size_t>(ch) * buckets * 2u;
            for (std::size_t b = b0; b < b1; ++b) {
                mn = std::min(mn, data[base + b * 2u]);
                mx = std::max(mx, data[base + b * 2u + 1u]);
            }
        }
        if (!(mn <= mx)) {
            mn = 0.0f;
            mx = 0.0f;
        }
        row.minmax[static_cast<std::size_t>(c) * 2u] =
            std::clamp(mn, -1.0f, 1.0f);
        row.minmax[static_cast<std::size_t>(c) * 2u + 1u] =
            std::clamp(mx, -1.0f, 1.0f);
    }
    *slot = std::move(row);
    return slot;
}

const video::Frame* TimelineWidget::PosterFor(const std::string& path,
                                              double seconds) {
    if (!hooks_.get_poster || path.empty()) return nullptr;
    const std::int64_t quantized =
        static_cast<std::int64_t>(
            std::llround(seconds / kPosterQuantize)) *
        static_cast<std::int64_t>(kPosterQuantize * 1000.0);
    const PosterKey key{path, quantized};
    const std::string miss_key =
        path + "@" + std::to_string(quantized);
    if (poster_missing_.count(miss_key) != 0u) {
        return NearestPoster(path, quantized);
    }
    const auto it = poster_cache_.find(key);
    if (it != poster_cache_.end()) {
        // Hit promotes to MRU — visible thumbs stay cached.
        poster_lru_.splice(poster_lru_.end(), poster_lru_,
                           it->second.lru_it);
        return it->second.frame.get();
    }
    auto frame = std::make_shared<video::Frame>();
    switch (hooks_.get_poster(path, seconds, frame.get())) {
        case MediaLookup::kReady:
            if (frame->IsValid() && frame->Width() > 0 &&
                frame->Height() > 0 &&
                frame->Format() == video::PixelFormat::BGRA8) {
                poster_lru_.push_back(key);
                PosterEntry entry;
                entry.frame = frame;
                entry.lru_it = std::prev(poster_lru_.end());
                poster_cache_[key] = entry;
                while (poster_lru_.size() > kMaxPosters) {
                    poster_cache_.erase(poster_lru_.front());
                    poster_lru_.pop_front();
                }
                return frame.get();
            }
            // unusable payload
            TrackMissing(poster_missing_, poster_missing_lru_, miss_key,
                         kMaxMissingKeys);
            return NearestPoster(path, quantized);
        case MediaLookup::kPending:
            // Worker decoding — draw the closest frame we already
            // have for this media so the slot never blanks.
            return NearestPoster(path, quantized);
        case MediaLookup::kFailed:
            TrackMissing(poster_missing_, poster_missing_lru_, miss_key,
                         kMaxMissingKeys);
            return NearestPoster(path, quantized);
    }
    return nullptr;
}

const video::Frame* TimelineWidget::NearestPoster(
    const std::string& path, std::int64_t quantized_ms) {
    const video::Frame* best = nullptr;
    std::int64_t best_dist = std::numeric_limits<std::int64_t>::max();
    auto best_it = poster_lru_.end();
    for (auto lit = poster_lru_.begin(); lit != poster_lru_.end();
         ++lit) {
        if (lit->path != path) continue;
        const auto mit = poster_cache_.find(*lit);
        if (mit == poster_cache_.end()) continue;
        const std::int64_t dist =
            lit->quantized_ms > quantized_ms
                ? lit->quantized_ms - quantized_ms
                : quantized_ms - lit->quantized_ms;
        if (dist < best_dist) {
            best_dist = dist;
            best = mit->second.frame.get();
            best_it = lit;
        }
    }
    if (best != nullptr) {
        // The fallback frame is on screen — promote it too.
        poster_lru_.splice(poster_lru_.end(), poster_lru_, best_it);
    }
    return best;
}

// ---- rendering ----

void TimelineWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const Rect bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (project_ == nullptr) return;

    const float lane_bottom = bounds.y + kMarkerLaneHeight;
    const float ruler_bottom = lane_bottom + kRulerHeight;

    // ---- chrome bands ----
    // The marker lane + ruler read as one raised chrome strip; the
    // header column is its left extension, separated by a hairline.
    renderer.FillRect({bounds.x, bounds.y, bounds.width,
                       ruler_bottom - bounds.y},
                      tokens.panel, 0.0f);
    renderer.DrawLine({bounds.x + kHeaderWidth, ruler_bottom},
                      {bounds.x + bounds.width, ruler_bottom},
                      tokens.border, 1.0f);

    // Corner box: caption + active-tool chip on the first line, the
    // ghost toolbar row (magnet/razor/marker/fx) underneath.
    const Rect caption{bounds.x + VeTheme::kSpaceMd, bounds.y + 3.0f,
                       kHeaderWidth - VeTheme::kSpaceMd * 2.0f, 14.0f};
    renderer.DrawText("TIMELINE", caption,
                      {tokens.muted, tokens.font_small, 700});
    if (const EditTool* tool = ActiveToolImpl()) {
        const std::string tool_name = tool->Name();
        const auto name_size = renderer.MeasureText(
            tool_name, {kAccentCyan, tokens.font_small, 600});
        const auto caption_size = renderer.MeasureText(
            "TIMELINE", {tokens.muted, tokens.font_small, 700});
        const Rect chip{caption.x + caption_size.width + 8.0f,
                        bounds.y + 2.0f, name_size.width + 10.0f, 15.0f};
        renderer.FillRect(chip, WithAlpha(kAccentCyan, 0.16f),
                          tokens.radius_small);
        renderer.StrokeRect(chip, WithAlpha(kAccentCyan, 0.45f), 1.0f,
                            tokens.radius_small);
        renderer.DrawText(
            tool_name,
            {chip.x + 5.0f, chip.y + 1.0f, chip.width - 10.0f,
             chip.height - 2.0f},
            {kAccentCyan, tokens.font_small, 600});
    }
    RenderToolbar(renderer, tokens, bounds);

    // ---- ruler ----
    // Adaptive major step for labels, plus quarter subdivisions when
    // they stay visually separated (no sub-ticks under ~14px).
    static constexpr double kSteps[] = {0.1, 0.25, 0.5, 1.0, 2.0,
                                        5.0, 10.0, 30.0, 60.0, 120.0};
    double step = 1.0;
    for (double candidate : kSteps) {
        if (candidate * px_per_second_ >= 84.0) {
            step = candidate;
            break;
        }
    }
    const double visible_end =
        scroll_seconds_ + timeline_area_.width / px_per_second_;
    const bool sub_ticks = step * 0.25 * px_per_second_ >= 14.0;
    if (sub_ticks) {
        double minor =
            std::floor(scroll_seconds_ / (step * 0.25)) * step * 0.25;
        for (; minor <= visible_end + step; minor += step * 0.25) {
            // Skip positions that coincide with a major tick.
            if (std::fabs(minor / step - std::round(minor / step)) <
                1e-6) {
                continue;
            }
            const float x = XAtTime(minor);
            renderer.DrawLine({x, ruler_bottom - 4.0f},
                              {x, ruler_bottom - 1.0f},
                              WithAlpha(tokens.muted, 0.35f), 1.0f);
        }
    }
    double tick = std::floor(scroll_seconds_ / step) * step;
    for (; tick <= visible_end + step; tick += step) {
        const float x = XAtTime(tick);
        renderer.DrawLine({x, ruler_bottom - 8.0f},
                          {x, ruler_bottom - 1.0f},
                          WithAlpha(kAccentCyan, 0.7f), 1.0f);
        // Monospaced cyan timecode marks (reference ruler style),
        // 4px right of the tick.
        char label[32];
        if (step < 1.0) {
            std::snprintf(label, sizeof(label), "%.2fs", tick);
        } else {
            const int minutes = static_cast<int>(tick / 60.0);
            const int seconds = static_cast<int>(std::fmod(tick, 60.0));
            std::snprintf(label, sizeof(label), "%d:%02d", minutes,
                          seconds);
        }
        renderer.DrawText(label,
                          Rect{x + 4.0f, lane_bottom + kWorkBarHeight +
                                              1.0f,
                               56.0f, 14.0f},
                          {kAccentCyan, tokens.font_small, 500, true});
    }

    // Header-column divider spanning marker lane + tracks.
    renderer.DrawLine({bounds.x + kHeaderWidth, bounds.y},
                      {bounds.x + kHeaderWidth,
                       bounds.y + bounds.height},
                      tokens.border, 1.0f);

    RenderMarkers(renderer, tokens, bounds);
    RenderWorkArea(renderer, tokens, bounds);

    // ---- track rows + headers ----
    // Both scroll vertically under the fixed ruler/marker chrome —
    // clip the whole band (header column and track area alike) to the
    // region below the ruler.
    renderer.PushClip({timeline_area_.x, ruler_bottom,
                       timeline_area_.width,
                       std::max(0.0f, bounds.y + bounds.height -
                                          ruler_bottom)});
    for (std::size_t i = 0; i < project_->tracks.size(); ++i) {
        const Track& track = project_->tracks[i];
        const bool is_video = track.kind == TrackKind::Video;
        const Color kind = KindColor(is_video);
        const Rect row = TrackRect(static_cast<int>(i));
        const Rect label_rect = HeaderRect(static_cast<int>(i));

        // Header: sunken card tinted by track kind, with a colored
        // kind bar on its left edge.
        renderer.FillRect(label_rect, tokens.input,
                          tokens.radius_medium);
        renderer.FillRect(label_rect,
                          WithAlpha(kind, is_video ? 0.07f : 0.09f),
                          tokens.radius_medium);
        renderer.StrokeRect(label_rect, tokens.border, 1.0f,
                            tokens.radius_medium);
        renderer.FillRect(
            Rect{label_rect.x, label_rect.y + 4.0f, kKindBarWidth,
                 label_rect.height - 8.0f},
            WithAlpha(kind, track.locked ? 0.4f : 0.9f), 1.5f);

        // Track-id badge chip ("V1" blue / "A1" teal): kind letter +
        // per-kind ordinal, not the arbitrary track name.
        int ordinal = 0;
        for (std::size_t j = 0; j <= i; ++j) {
            if (project_->tracks[j].kind == track.kind) ++ordinal;
        }
        char badge_text[8];
        std::snprintf(badge_text, sizeof(badge_text), "%c%d",
                      is_video ? 'V' : 'A', ordinal);
        const auto badge_size = renderer.MeasureText(
            badge_text,
            {Color{1, 1, 1, 0.95f}, tokens.font_small, 700});
        const Rect badge{label_rect.x + kKindBarWidth + 8.0f,
                         label_rect.y + 8.0f,
                         badge_size.width + 12.0f, 20.0f};
        renderer.FillRect(badge, WithAlpha(kind, 0.9f), 5.0f);
        DrawCenteredText(renderer, badge_text,
                         Rect{badge.x, badge.y + 2.5f, badge.width,
                              badge.height},
                         {Color{1.0f, 1.0f, 1.0f, 0.95f},
                          tokens.font_small, 700});

        // Track name on the badge's right when it adds information
        // (duplicate of the badge id is hidden — "V1" over a "V1"
        // chip reads as noise); state flags on the line below.
        const float name_x = badge.x + badge.width + 8.0f;
        const float name_right =
            label_rect.x + label_rect.width - 24.0f;   // chevron lane
        const bool name_differs = track.name != badge_text;
        if (name_differs && !track.name.empty()) {
            renderer.DrawText(
                track.name,
                Rect{name_x, label_rect.y + 7.0f,
                     std::max(0.0f, name_right - name_x), 18.0f},
                {track.locked ? tokens.muted : tokens.foreground,
                 tokens.font_small, 600});
        }
        std::string flags;
        if (track.muted) flags += "muted  ";
        if (track.hidden) flags += "hidden  ";
        if (track.locked) flags += "locked";
        if (flags.empty()) {
            flags = is_video ? "video" : "audio";
        }
        renderer.DrawText(
            flags,
            Rect{name_x, label_rect.y + 24.0f,
                 label_rect.x + label_rect.width - name_x - 6.0f,
                 15.0f},
            {flags.front() == 'v' || flags.front() == 'a'
                 ? tokens.disabled
                 : tokens.danger,
             tokens.font_small, 400});
        RenderHeaderButtons(renderer, tokens, track,
                            static_cast<int>(i), label_rect);

        // Expand chevron at the header's top-right corner — swaps the
        // lane between compact and tall (waveform/thumbnail room).
        const bool expanded = expanded_tracks_.count(i) != 0u;
        const Rect chev = HeaderChevronRect(label_rect);
        const bool chev_hot = hover_header_.track ==
                static_cast<int>(i) &&
            hover_header_.button == 3;
        if (expanded || chev_hot) {
            renderer.FillRect(chev, tokens.hover, 4.0f);
        }
        DrawChevronIcon(renderer, chev,
                        expanded ? kAccentCyan : tokens.muted,
                        expanded);

        // Track lane: sunken fill with a whisper of the kind tint.
        Color row_color =
            VeTheme::Mix(tokens.input, kind, is_video ? 0.05f : 0.07f);
        if (track.locked) {
            row_color = WithAlpha(row_color, 0.6f);
        }
        renderer.FillRect(row, row_color, tokens.radius_small);
    }
    renderer.PopClip();   // rows/header scroll band

    // ---- clips ----
    // Clips render inside the track-area viewport only — a clip that
    // starts before the scroll offset must not bleed under the header
    // column.
    const Rect tracks_viewport{timeline_area_.x + kHeaderWidth,
                               ruler_bottom,
                               std::max(0.0f, timeline_area_.width -
                                                  kHeaderWidth),
                               std::max(0.0f, bounds.y + bounds.height -
                                                  ruler_bottom)};
    renderer.PushClip(tracks_viewport);
    for (const Clip& clip : project_->clips) {
        if (clip.track < 0 ||
            clip.track >= static_cast<int>(project_->tracks.size())) {
            continue;
        }
        const Track& track = project_->tracks[clip.track];
        const Rect rect = ClipRect(clip);
        if (rect.x + rect.width < timeline_area_.x + kHeaderWidth ||
            rect.x > timeline_area_.x + timeline_area_.width) {
            continue;
        }
        const Color base = ClipBaseFor(track, clip.track);
        // The label strip is a darkened shade of the clip color — the
        // classic NLE "header band" look.
        const Color band = VeTheme::Mix(base, Color{0, 0, 0, 1}, 0.30f);
        const bool previewed = clip_previews_.count(clip.id) != 0u;
        if (previewed) {
            // Original position stays as a dim ghost under the
            // previewed span (Premiere's edit preview).
            const Rect real = RealClipRect(clip);
            renderer.FillRect(real, WithAlpha(base, 0.14f),
                              kClipRadius);
            renderer.StrokeRect(real, WithAlpha(tokens.border, 0.5f),
                                1.0f, kClipRadius);
        }
        float alpha = clip.muted || track.hidden ? 0.30f : 1.0f;
        if (track.locked) alpha *= 0.5f;   // locked tracks dim
        // Two-tone clip: fill the whole rounded rect in the darker
        // band color, then lay the lighter body over everything below
        // the label strip. The body's square top corners hide inside
        // the band; its rounded bottom corners match the outline.
        renderer.FillRect(rect, WithAlpha(band, alpha), kClipRadius);
        const Rect body{rect.x, rect.y + kClipLabelHeight, rect.width,
                        rect.height - kClipLabelHeight};
        if (body.height > 0.0f) {
            renderer.FillRect(body, WithAlpha(base, alpha * 0.62f),
                              kClipRadius);
        }
        const bool selected = clip.id == selected_clip_id_;
        const bool hovered =
            !selected && clip.id == hover_clip_id_ && !previewed;
        const Color stroke =
            previewed ? tokens.accent
                      : selected
                            ? kAccentCyan
                            : hovered
                                  ? WithAlpha(Color{1, 1, 1, 1}, 0.5f)
                                  : WithAlpha(tokens.border, 0.8f);
        renderer.StrokeRect(
            rect, stroke,
            previewed || selected ? 2.0f : 1.0f, kClipRadius);

        // Content region below the label strip, clipped to the clip
        // rect (waveforms/posters stay inside the rounded bounds).
        const Rect content{rect.x + 1.0f,
                           rect.y + kClipLabelHeight + 1.0f,
                           std::max(0.0f, rect.width - 2.0f),
                           std::max(0.0f, rect.height -
                                              kClipLabelHeight - 2.0f)};
        renderer.PushClip(content);
        if (track.kind == TrackKind::Audio) {
            RenderWaveform(renderer, tokens, clip, content);
        } else {
            RenderThumbnails(renderer, tokens, clip, content);
        }
        renderer.PopClip();

        // Clip name inside the band — 6px left inset, vertically
        // centered; never touches the rounded corners. Right-edge
        // chips stack inward: "lock" tag, amber "fx", link rings.
        const Color label_color =
            track.locked || clip.muted || track.hidden
                ? WithAlpha(Color{1, 1, 1, 1}, 0.62f)
                : Color{1.0f, 1.0f, 1.0f, 0.96f};
        const bool has_fx =
            !clip.effects.empty() || !clip.audio_fx.empty();
        // Junction badges straddle the cut on the label strip —
        // reserve room: out-transition pushes the right chips left,
        // in-transition pushes the name right.
        bool trans_in = false, trans_out = false;
        for (const Transition& tr : project_->transitions) {
            if (tr.from_clip == clip.id) trans_out = true;
            if (tr.to_clip == clip.id) trans_in = true;
        }
        float right_cursor =
            rect.x + rect.width - 5.0f - (trans_out ? 14.0f : 0.0f);
        const float left_cursor =
            rect.x + 6.0f + (trans_in ? 14.0f : 0.0f);
        if (track.locked && rect.width > 70.0f) {
            renderer.DrawText("lock",
                              Rect{right_cursor - 25.0f, rect.y + 2.0f,
                                   26.0f, kClipLabelHeight - 3.0f},
                              {WithAlpha(tokens.danger, 0.9f),
                               tokens.font_small, 600});
            right_cursor -= 30.0f;
        }
        if (has_fx && rect.width > 46.0f) {
            // Amber "fx" chip — clip carries an effect/audio-fx chain.
            const Rect fx{right_cursor - 18.0f, rect.y + 3.0f, 18.0f,
                          12.0f};
            renderer.FillRect(fx, WithAlpha(kFxAmber, 0.92f), 3.0f);
            DrawCenteredText(
                renderer, "fx",
                Rect{fx.x, fx.y + 0.5f, fx.width, fx.height},
                {Color{0.10f, 0.07f, 0.0f, 1.0f}, tokens.font_small,
                 700});
            right_cursor -= 22.0f;
        }
        if (!clip.link_group.empty() && rect.width > 44.0f &&
            right_cursor - 16.0f > left_cursor + 2.0f) {
            const float cy = rect.y + kClipLabelHeight * 0.5f + 0.5f;
            const float lx = right_cursor - 12.0f;
            const Color link =
                WithAlpha(label_color, label_color.a * 0.9f);
            renderer.StrokeRect(
                Rect{lx - 5.0f, cy - 3.0f, 6.5f, 6.0f}, link, 1.1f,
                3.0f);
            renderer.StrokeRect(
                Rect{lx + 0.5f, cy - 3.0f, 6.5f, 6.0f}, link, 1.1f,
                3.0f);
            right_cursor -= 18.0f;
        }
        renderer.DrawText(
            ClipLabel(clip),
            Rect{left_cursor, rect.y + 2.0f,
                 std::max(0.0f, right_cursor - left_cursor - 2.0f),
                 kClipLabelHeight - 3.0f},
            {label_color, tokens.font_small, 600});
        // Duration/rate sub-line under the band, left-aligned with
        // the name's 6px inset.
        std::string sub = FormatTime(clip.duration);
        if (std::fabs(clip.playback_rate - 1.0) > 1e-6) {
            char rate[16];
            std::snprintf(rate, sizeof(rate), "  %.2fx",
                          clip.playback_rate);
            sub += rate;
        }
        renderer.DrawText(
            sub,
            Rect{rect.x + 6.0f, rect.y + kClipLabelHeight + 3.0f,
                 std::max(0.0f, rect.width - 12.0f), 14.0f},
            {WithAlpha(Color{1, 1, 1, 1},
                       label_color.a * 0.75f),
             tokens.font_small, 400});

        // Trim affordances — subtle bright grips hugging both edges.
        renderer.FillRect(
            Rect{rect.x + 1.0f, rect.y + 4.0f, 3.0f,
                 rect.height - 8.0f},
            WithAlpha(Color{1, 1, 1, 1}, 0.28f), 1.5f);
        renderer.FillRect(
            Rect{rect.x + rect.width - 4.0f, rect.y + 4.0f, 3.0f,
                 rect.height - 8.0f},
            WithAlpha(Color{1, 1, 1, 1}, 0.28f), 1.5f);
    }

    // Transition badges at cuts — small rounded chips straddling the
    // junction on the clip's top edge.
    for (const Transition& transition : project_->transitions) {
        const Clip* from = FindClip(*project_, transition.from_clip);
        const Clip* to = FindClip(*project_, transition.to_clip);
        if (from == nullptr || to == nullptr) continue;
        const float x = XAtTime(ClipEndSeconds(*from));
        const Rect row = TrackRect(from->track);
        const Rect badge{x - 9.0f, row.y + kClipInset + 1.0f, 18.0f,
                         14.0f};
        renderer.FillRect(badge, WithAlpha(tokens.accent, 0.9f), 4.0f);
        renderer.StrokeRect(badge, WithAlpha(tokens.border, 0.7f), 1.0f,
                            4.0f);
        DrawCenteredText(renderer, "T",
                         Rect{badge.x, badge.y + 0.5f, badge.width,
                              badge.height},
                         {tokens.accent_foreground, tokens.font_small,
                          700});
    }
    renderer.PopClip();   // tracks_viewport

    // Zoom marquee (rubber band) across the full track area.
    if (marquee_active_ && marquee_x1_ > marquee_x0_) {
        const Rect band{marquee_x0_, ruler_bottom,
                        marquee_x1_ - marquee_x0_,
                        bounds.y + bounds.height - ruler_bottom};
        renderer.FillRect(band, WithAlpha(tokens.accent, 0.12f), 0.0f);
        renderer.StrokeRect(band, tokens.accent, 1.0f, 0.0f);
    }

    // Playhead: bright cyan line with a pill-shaped cap handle in the
    // ruler/marker band (reference NLE style).
    const float play_x = XAtTime(playhead_);
    renderer.FillRect(
        Rect{play_x - 7.0f, bounds.y + 1.0f, 14.0f, 14.0f},
        kAccentCyan, 7.0f);
    renderer.DrawLine({play_x, bounds.y + 5.0f},
                      {play_x, bounds.y + bounds.height}, kAccentCyan,
                      2.0f);

    // Media-bin drag hovering this widget — accent frame marks the
    // panel as a live drop target (SetExternalDropHover).
    if (external_drop_hover_) {
        renderer.StrokeRect(bounds, tokens.accent, 2.0f,
                            tokens.radius_large);
    }
}

void TimelineWidget::RenderMarkers(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Rect& bounds) {
    const Rect lane{timeline_area_.x + kHeaderWidth, bounds.y,
                    std::max(0.0f, bounds.width - kHeaderWidth),
                    kMarkerLaneHeight};
    renderer.FillRect(lane, WithAlpha(tokens.background, 0.55f), 0.0f);
    renderer.DrawLine({lane.x, bounds.y + kMarkerLaneHeight},
                      {lane.x + lane.width, bounds.y + kMarkerLaneHeight},
                      tokens.border, 1.0f);
    const float mid = bounds.y + kMarkerLaneHeight * 0.5f;
    for (const Marker& marker : project_->markers) {
        const float x = XAtTime(marker.time);
        if (x < lane.x - 8.0f || x > lane.x + lane.width + 8.0f) {
            continue;
        }
        const Color color = MarkerColor(marker);
        DrawDiamond(renderer, {x, mid}, 5.0f, color, 1.5f);
        if (!marker.name.empty()) {
            renderer.DrawText(marker.name,
                              Rect{x + 8.0f, bounds.y + 2.0f, 120.0f,
                                   kMarkerLaneHeight - 3.0f},
                              {color, tokens.font_small, 500});
        }
    }
}

void TimelineWidget::RenderWorkArea(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Rect& bounds) {
    const double in =
        work_drag_ != WorkDrag::None ? work_drag_in_ : project_->work_in;
    const double out = work_drag_ != WorkDrag::None ? work_drag_out_
                                                    : project_->work_out;
    if (out <= in) return;   // work_out <= work_in = whole project
    const float xi = XAtTime(in);
    const float xo = XAtTime(out);
    const float band_top = bounds.y + kMarkerLaneHeight + 1.0f;
    const Rect band{xi, band_top, std::max(1.0f, xo - xi),
                    kWorkBarHeight - 2.0f};
    renderer.FillRect(band, WithAlpha(tokens.accent, 0.22f), 2.0f);
    renderer.DrawLine({xi, band_top}, {xo, band_top}, tokens.accent,
                      2.0f);
    // Edge handles — the drag grab zones TopZoneAt reports.
    renderer.FillRect(Rect{xi - 3.0f, band_top - 1.0f, 6.0f,
                           kWorkBarHeight},
                      tokens.accent, 1.0f);
    renderer.FillRect(Rect{xo - 3.0f, band_top - 1.0f, 6.0f,
                           kWorkBarHeight},
                      tokens.accent, 1.0f);
}

void TimelineWidget::RenderHeaderButtons(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Track& track, int track_index,
    const Rect& label_rect) {
    // Logical-id -> live state (solo/arm draw but stay unwired — the
    // track model has no fields for them yet; clicks explain via
    // status).
    const auto is_active = [&track](int button) {
        switch (button) {
            case 0: return track.muted;
            case 1: return track.hidden;
            case 2: return track.locked;
            default: return false;
        }
    };
    const Color kind = KindColor(track.kind == TrackKind::Video);
    const int count = HeaderButtonCount(track);
    for (int slot = 0; slot < count; ++slot) {
        const int button = HeaderButtonIdFor(track, slot);
        const Rect r = HeaderButtonRect(label_rect, slot, count);
        const bool active = is_active(button);
        const bool hot = hover_header_.track == track_index &&
                         hover_header_.button == button;
        // Ghost affordance: no chrome until hovered; active toggles
        // get a tinted chip (lock reads danger, kind toggles the kind
        // color).
        Color glyph = hot ? tokens.foreground : tokens.muted;
        if (active) {
            const Color fill =
                button == 2 ? tokens.danger : WithAlpha(kind, 0.9f);
            renderer.FillRect(r, fill, tokens.radius_small);
            glyph = Color{1.0f, 1.0f, 1.0f, 0.96f};
        } else if (hot) {
            renderer.FillRect(r, tokens.hover, tokens.radius_small);
        }
        const Rect box{r.x + 2.0f, r.y + 2.0f, r.width - 4.0f,
                       r.height - 4.0f};
        switch (button) {
            case 0:   // M — mute
                DrawCenteredText(renderer, "M",
                                 Rect{r.x, r.y + 1.5f, r.width,
                                      r.height},
                                 {glyph, tokens.font_small, 700});
                break;
            case 4:   // S — solo
                DrawCenteredText(renderer, "S",
                                 Rect{r.x, r.y + 1.5f, r.width,
                                      r.height},
                                 {glyph, tokens.font_small, 700});
                break;
            case 5:   // mic — record arm
                DrawMicIcon(renderer, box, glyph);
                break;
            case 1:   // eye — visibility (slashed when hidden)
                DrawEyeIcon(renderer, box, glyph, track.hidden);
                break;
            case 2:   // lock
                DrawLockIcon(renderer, box, glyph);
                break;
            default:
                break;
        }
    }
}

void TimelineWidget::RenderToolbar(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Rect& bounds) {
    (void)bounds;
    // Ghost icon row under the caption: magnet (snap), razor, marker
    // at playhead, fx badge. Same affordance language as the nav rail:
    // no chrome until hovered, tinted chip while the state is on.
    for (int i = 0; i < kToolButtonCount; ++i) {
        const Rect r = ToolbarButtonRect(i);
        const bool hot = hover_toolbar_ == i;
        const bool active =
            (i == 0 && snap_enabled_) ||
            (i == 1 && active_tool_ == ToolId::Razor);
        if (active) {
            renderer.FillRect(r, WithAlpha(kAccentCyan, 0.18f),
                              tokens.radius_small);
            renderer.StrokeRect(r, WithAlpha(kAccentCyan, 0.55f), 1.0f,
                                tokens.radius_small);
        } else if (hot) {
            renderer.FillRect(r, tokens.hover, tokens.radius_small);
        }
        const Color glyph =
            active ? kAccentCyan
                   : (hot ? tokens.foreground : tokens.muted);
        const Rect box{r.x + 2.0f, r.y + 2.0f, r.width - 4.0f,
                       r.height - 4.0f};
        switch (i) {
            case 0:
                DrawMagnetIcon(renderer, box, glyph);
                break;
            case 1:
                DrawRazorIcon(renderer, box, glyph);
                break;
            case 2:
                DrawDiamond(
                    renderer,
                    Point{box.x + box.width * 0.5f,
                          box.y + box.height * 0.5f},
                    4.0f, glyph, 1.4f);
                break;
            case 3: {
                // fx badge chip — amber, matching the per-clip chips.
                const Rect chip{r.x, r.y + 4.0f, r.width, 12.0f};
                renderer.FillRect(chip, WithAlpha(kFxAmber,
                                                  hot ? 1.0f : 0.8f),
                                  3.0f);
                DrawCenteredText(
                    renderer, "fx",
                    Rect{chip.x, chip.y, chip.width, chip.height},
                    {Color{0.10f, 0.07f, 0.0f, 1.0f},
                     tokens.font_small, 700});
                break;
            }
            default:
                break;
        }
    }
}

void TimelineWidget::RenderWaveform(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Clip& clip,
    const Rect& rect) {
    (void)tokens;
    if (!hooks_.get_audio_peaks || clip.source_path.empty() ||
        clip.source.kind != SourceKind::Media) {
        return;
    }
    const int columns = std::max(1, static_cast<int>(rect.width));
    const WaveRow* wave = WaveFor(clip.source_path, columns);
    if (wave == nullptr || wave->span_seconds <= 0.0) return;
    const double src0 = clip.in_point;
    const double src1 =
        clip.in_point + clip.duration * clip.playback_rate;
    if (!(src1 > src0)) return;
    const float mid = rect.y + rect.height * 0.58f;
    const float half = rect.height * 0.38f;
    // Bright teal min/max strokes on the dark-teal clip body (the
    // reference's in-clip waveform read).
    const Color color = WithAlpha(kWaveformTeal, 0.85f);
    renderer.DrawLine({rect.x, mid}, {rect.x + rect.width, mid},
                      WithAlpha(kWaveformTeal, 0.25f), 1.0f);
    for (int px = 0; px < columns; ++px) {
        const double t0 = src0 + (src1 - src0) * px / columns;
        const double t1 = src0 + (src1 - src0) * (px + 1) / columns;
        int c0 = static_cast<int>(
            std::floor(t0 / wave->span_seconds * wave->columns));
        int c1 = static_cast<int>(
                     std::ceil(t1 / wave->span_seconds * wave->columns)) -
                 1;
        c0 = std::clamp(c0, 0, wave->columns - 1);
        c1 = std::clamp(c1, c0, wave->columns - 1);
        float mn = 1.0f, mx = -1.0f;
        for (int c = c0; c <= c1; ++c) {
            mn = std::min(mn, wave->minmax[static_cast<std::size_t>(c) * 2u]);
            mx = std::max(
                mx, wave->minmax[static_cast<std::size_t>(c) * 2u + 1u]);
        }
        const float x = rect.x + static_cast<float>(px) + 0.5f;
        float y0 = mid - mx * half;
        float y1 = mid - mn * half;
        if (y1 - y0 < 1.0f) {
            y0 = mid - 0.5f;
            y1 = mid + 0.5f;
        }
        renderer.DrawLine({x, y0}, {x, y1}, color, 1.0f);
    }
}

void TimelineWidget::RenderThumbnails(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Clip& clip,
    const Rect& rect) {
    (void)tokens;
    if (!hooks_.get_poster || clip.source_path.empty() ||
        clip.source.kind != SourceKind::Media) {
        return;
    }
    // `rect` is the clip's content region (below the label strip).
    const float slot_h = rect.height - 6.0f;
    if (slot_h < 10.0f || px_per_second_ <= 0.0) return;
    const float slot_w = std::max(24.0f, slot_h * (16.0f / 9.0f));
    for (float x = rect.x + 3.0f; x + 8.0f < rect.x + rect.width;
         x += slot_w + 4.0f) {
        const double center_offset =
            (x + slot_w * 0.5f - rect.x) / px_per_second_;
        const double source_t =
            clip.in_point + center_offset * clip.playback_rate;
        const video::Frame* frame =
            PosterFor(clip.source_path, source_t);
        if (frame == nullptr) continue;
        const float aspect =
            static_cast<float>(frame->Width()) / frame->Height();
        float dst_w = slot_w, dst_h = slot_h;
        if (aspect > slot_w / slot_h) {
            dst_h = slot_w / aspect;
        } else {
            dst_w = slot_h * aspect;
        }
        const Rect dst{x + (slot_w - dst_w) * 0.5f,
                       rect.y + 3.0f + (slot_h - dst_h) * 0.5f,
                       dst_w, dst_h};
        renderer.DrawFrameBgra(frame->Data(), frame->Width(),
                               frame->Height(), dst, 0.9f);
    }
}

// ---- input ----

void TimelineWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    timeline_area_ = bounds;
}

bool TimelineWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::KeyCode;
    using XER::UI::Widgets::MouseButton;
    if (!IsEnabled() || project_ == nullptr) return false;

    if (event.type == Event::Type::MouseWheel) {
        if (event.is_alt) {
            // Alt+wheel scrolls the track stack vertically — plain
            // wheel stays zoom, Shift+wheel stays horizontal scroll.
            const float ruler_h = kMarkerLaneHeight + kRulerHeight;
            const float viewport =
                std::max(0.0f, timeline_area_.height - ruler_h);
            float content = 0.0f;
            for (std::size_t i = 0; i < project_->tracks.size(); ++i) {
                content += TrackRowHeight(static_cast<int>(i)) +
                           kTrackGap;
            }
            const float max_scroll =
                std::max(0.0f, content - viewport);
            track_scroll_ = std::clamp(
                track_scroll_ -
                    event.wheel_delta / 120.0f *
                        (kTrackHeight + kTrackGap),
                0.0f, max_scroll);
            Invalidate();
            return true;
        }
        const double anchor = TimeAtX(event.position.x);
        if (event.is_shift) {
            scroll_seconds_ -= event.wheel_delta / 120.0;
        } else {
            const double factor =
                std::pow(1.16, event.wheel_delta / 120.0);
            const double next =
                std::clamp(px_per_second_ * factor, kMinZoom, kMaxZoom);
            px_per_second_ = next;
            scroll_seconds_ = std::max(
                0.0, anchor -
                         (event.position.x -
                          (timeline_area_.x + kHeaderWidth)) /
                             px_per_second_);
        }
        scroll_seconds_ = std::max(0.0, scroll_seconds_);
        Invalidate();
        return true;
    }

    const Point p = event.position;
    const float lane_bottom = timeline_area_.y + kMarkerLaneHeight;
    const float ruler_bottom = lane_bottom + kRulerHeight;
    const bool in_top_strip =
        p.x >= timeline_area_.x + kHeaderWidth &&
        p.y >= timeline_area_.y && p.y < ruler_bottom;
    const bool in_lane = in_top_strip && p.y < lane_bottom;

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Right) {
        // Right-clicks fall through to the shell's tree-level
        // context-menu handler, which calls back into
        // ShowContextMenu() for geometry-aware items.
        return false;
    }

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        SetFocused(true);

        // Corner-toolbar ghost buttons (magnet/razor/marker/fx).
        if (const int tool_btn = ToolbarButtonAt(p); tool_btn >= 0) {
            switch (tool_btn) {
                case 0:
                    if (hooks_.on_snap_toggle) {
                        hooks_.on_snap_toggle(!snap_enabled_);
                    } else {
                        SetSnapEnabled(!snap_enabled_);
                    }
                    break;
                case 1:
                    SetTool(ToolId::Razor);
                    break;
                case 2:
                    if (hooks_.on_add_marker) {
                        hooks_.on_add_marker(playhead_, "");
                    }
                    break;
                case 3: {
                    int fx = 0;
                    for (const Clip& clip : project_->clips) {
                        if (!clip.effects.empty() ||
                            !clip.audio_fx.empty()) {
                            ++fx;
                        }
                    }
                    SetStatus(std::to_string(fx) +
                              (fx == 1 ? " clip carries effects"
                                       : " clips carry effects"));
                    break;
                }
                default:
                    break;
            }
            Invalidate();
            return true;
        }

        // Track-header affordances (mute / hidden-eye / lock /
        // expand chevron / solo / record arm).
        if (const HeaderHit hit = HitHeaderButton(p);
            hit.track >= 0 && hit.button >= 0) {
            const Track& track = project_->tracks[hit.track];
            if (!HeaderButtonApplies(track, hit.button)) {
                return true;   // dead chip — swallow, never toggles
            }
            if (hit.button == 0 && hooks_.on_track_mute) {
                hooks_.on_track_mute(hit.track, !track.muted);
            } else if (hit.button == 1 && hooks_.on_track_hidden) {
                hooks_.on_track_hidden(hit.track, !track.hidden);
            } else if (hit.button == 2 && hooks_.on_track_locked) {
                hooks_.on_track_locked(hit.track, !track.locked);
            } else if (hit.button == 3) {
                // Expand chevron — taller lane, widget-side only.
                if (expanded_tracks_.count(hit.track) != 0u) {
                    expanded_tracks_.erase(hit.track);
                } else {
                    expanded_tracks_.insert(hit.track);
                }
            } else if (hit.button == 4) {
                SetStatus("Solo isn't wired into the track model yet");
            } else if (hit.button == 5) {
                SetStatus("Record arm isn't wired into the track "
                          "model yet");
            }
            Invalidate();
            return true;
        }

        if (in_top_strip) {
            const double t = TimeAtX(p.x);
            // Double-click on the ruler or marker lane drops a marker.
            const auto now = std::chrono::steady_clock::now();
            const bool double_click =
                last_ruler_click_valid_ &&
                now - last_ruler_click_ < kDoubleClick &&
                std::fabs(p.x - last_ruler_click_x_) <= 6.0f;
            last_ruler_click_ = now;
            last_ruler_click_x_ = p.x;
            last_ruler_click_valid_ = true;
            if (double_click) {
                last_ruler_click_valid_ = false;
                if (hooks_.on_add_marker) {
                    hooks_.on_add_marker(t, "");
                }
                Invalidate();
                return true;
            }
            // Work-area bar handles live in the ruler's top band.
            if (!in_lane) {
                switch (TopZoneAt(p)) {
                    case 1: work_drag_ = WorkDrag::In; break;
                    case 2: work_drag_ = WorkDrag::Out; break;
                    case 3: work_drag_ = WorkDrag::Move; break;
                    default: break;
                }
                if (work_drag_ != WorkDrag::None) {
                    work_drag_in_ = project_->work_in;
                    work_drag_out_ = project_->work_out;
                    work_drag_anchor_ = t;
                    work_drag_orig_in_ = project_->work_in;
                    work_drag_orig_out_ = project_->work_out;
                    return true;
                }
            }
            if (hooks_.on_seek) hooks_.on_seek(t);
            ruler_scrub_ = true;   // keep scrubbing while held
            return true;
        }

        // Pointer events inside the track area belong to the active
        // tool — it resolves hit/drag/commit via ToolCommit.
        const ToolContext ctx = MakeContext(event);
        EditTool* tool = ActiveToolImpl();
        const bool used =
            tool != nullptr && tool->OnPointerDown(ctx, *this);
        return used || ctx.track >= 0;
    }

    if (event.type == Event::Type::MouseMove) {
        // Hover affordances — clip border, header-button and toolbar
        // tints. Repaint only when the hit result actually changed.
        {
            const ClipHit hover = HitClip(p);
            const std::string hover_id =
                hover.clip != nullptr ? hover.clip->id : "";
            const HeaderHit hover_header = HitHeaderButton(p);
            const int hover_tool = ToolbarButtonAt(p);
            if (hover_id != hover_clip_id_ ||
                hover_header.track != hover_header_.track ||
                hover_header.button != hover_header_.button ||
                hover_tool != hover_toolbar_) {
                hover_clip_id_ = hover_id;
                hover_header_ = hover_header;
                hover_toolbar_ = hover_tool;
                Invalidate();
            }
        }
        if (ruler_scrub_) {
            if (hooks_.on_seek) hooks_.on_seek(TimeAtX(p.x));
            return true;
        }
        if (work_drag_ != WorkDrag::None) {
            const double t = SnapCandidate(TimeAtX(p.x), -1);
            switch (work_drag_) {
                case WorkDrag::In:
                    work_drag_in_ =
                        std::clamp(t, 0.0, work_drag_out_);
                    break;
                case WorkDrag::Out:
                    work_drag_out_ = std::max(t, work_drag_in_);
                    break;
                case WorkDrag::Move: {
                    double shift = t - work_drag_anchor_;
                    if (work_drag_orig_in_ + shift < 0.0) {
                        shift = -work_drag_orig_in_;
                    }
                    work_drag_in_ = work_drag_orig_in_ + shift;
                    work_drag_out_ = work_drag_orig_out_ + shift;
                    break;
                }
                default:
                    break;
            }
            SetStatus("Work area " + FormatTime(work_drag_in_) +
                      " - " + FormatTime(work_drag_out_));
            Invalidate();
            return true;
        }
        if (EditTool* tool = ActiveToolImpl();
            tool != nullptr && tool->IsDragging()) {
            const ToolContext ctx = MakeContext(event);
            return tool->OnPointerMove(ctx, *this);
        }
        return false;
    }

    if (event.type == Event::Type::MouseUp &&
        event.mouse_button == MouseButton::Left) {
        if (ruler_scrub_) {
            ruler_scrub_ = false;
            return true;
        }
        if (work_drag_ != WorkDrag::None) {
            work_drag_ = WorkDrag::None;
            const double in =
                std::min(work_drag_in_, work_drag_out_);
            const double out =
                std::max(work_drag_in_, work_drag_out_);
            if (hooks_.on_set_work_area) {
                hooks_.on_set_work_area(in, out);
            }
            Invalidate();
            return true;
        }
        if (EditTool* tool = ActiveToolImpl();
            tool != nullptr && tool->IsDragging()) {
            const ToolContext ctx = MakeContext(event);
            return tool->OnPointerUp(ctx, *this);
        }
        return false;
    }

    if (event.type == Event::Type::KeyDown) {
        if (event.key == KeyCode::Escape) {
            if (work_drag_ != WorkDrag::None) {
                work_drag_ = WorkDrag::None;
                Invalidate();
                return true;
            }
            if (EditTool* tool = ActiveToolImpl();
                tool != nullptr && tool->IsDragging()) {
                tool->Cancel(*this);
                return true;
            }
            return false;
        }
        const double frame_step = project_->fps_num > 0
            ? static_cast<double>(project_->fps_den) / project_->fps_num
            : 1.0 / 30.0;
        if (event.key == KeyCode::ArrowLeft ||
            event.key == KeyCode::ArrowRight) {
            const double direction =
                event.key == KeyCode::ArrowLeft ? -1.0 : 1.0;
            const double t = playhead_ +
                direction * (event.is_shift ? 1.0 : frame_step);
            if (hooks_.on_seek) hooks_.on_seek(t);
            return true;
        }
        if (event.key == KeyCode::Delete &&
            !selected_clip_id_.empty()) {
            // Alt+Delete = ripple delete (remove + close the gap);
            // plain Delete stays the classic remove.
            if (event.is_alt && hooks_.on_ripple_delete) {
                hooks_.on_ripple_delete(selected_clip_id_);
            } else if (hooks_.on_delete_clip) {
                hooks_.on_delete_clip(selected_clip_id_);
            }
            return true;
        }
    }
    return false;
}

// ---- ToolCommit ----

void TimelineWidget::SelectClip(const std::string& clip_id) {
    if (hooks_.on_select_clip) hooks_.on_select_clip(clip_id);
}

double TimelineWidget::SnapCandidate(double seconds, int track) const {
    if (!snap_enabled_ || project_ == nullptr) return seconds;
    // CollectSnapPoints already feeds clip edges on the track (all
    // tracks when < 0), marker times, work-area edges and the playhead.
    return SnapTime(*project_, track, seconds, 8.0 / px_per_second_,
                    playhead_);
}

void TimelineWidget::SetClipPreview(const std::string& clip_id,
                                    double start_seconds,
                                    double end_seconds) {
    clip_previews_[clip_id] = {start_seconds, end_seconds};
}

void TimelineWidget::ClearClipPreviews() { clip_previews_.clear(); }

void TimelineWidget::SetMarquee(bool active, float x0, float x1) {
    marquee_active_ = active;
    marquee_x0_ = x0;
    marquee_x1_ = x1;
}

void TimelineWidget::MoveClip(const std::string& clip_id,
                              double new_start) {
    if (hooks_.on_move_clip) hooks_.on_move_clip(clip_id, new_start);
}

void TimelineWidget::TrimClip(const std::string& clip_id, TrimEdge edge,
                              double new_point) {
    if (hooks_.on_trim_clip) {
        hooks_.on_trim_clip(clip_id, edge, new_point);
    }
}

void TimelineWidget::RippleTrim(const std::string& clip_id,
                                TrimEdge edge, double new_point) {
    if (hooks_.on_ripple_trim) {
        hooks_.on_ripple_trim(clip_id, edge, new_point);
    } else {
        TrimClip(clip_id, edge, new_point);
    }
}

void TimelineWidget::SplitClip(const std::string& clip_id,
                               double at_seconds) {
    if (hooks_.on_split_clip) hooks_.on_split_clip(clip_id, at_seconds);
}

void TimelineWidget::RollingEdit(const std::string& from_id,
                                 const std::string& to_id,
                                 double delta_seconds) {
    if (hooks_.on_rolling_edit) {
        hooks_.on_rolling_edit(from_id, to_id, delta_seconds);
    }
}

void TimelineWidget::SlipClip(const std::string& clip_id,
                              double delta_seconds) {
    if (hooks_.on_slip_clip) hooks_.on_slip_clip(clip_id, delta_seconds);
}

void TimelineWidget::SlideClip(const std::string& clip_id,
                               double delta_seconds) {
    if (hooks_.on_slide_clip) {
        hooks_.on_slide_clip(clip_id, delta_seconds);
    }
}

void TimelineWidget::SetClipRate(const std::string& clip_id,
                                 double new_rate) {
    if (hooks_.on_rate_stretch) {
        hooks_.on_rate_stretch(clip_id, new_rate);
    }
}

void TimelineWidget::PanBy(double seconds) {
    scroll_seconds_ = std::max(0.0, scroll_seconds_ + seconds);
    Invalidate();
}

void TimelineWidget::ZoomAt(double factor, double anchor_seconds) {
    const double next =
        std::clamp(px_per_second_ * factor, kMinZoom, kMaxZoom);
    if (next == px_per_second_) return;
    const float anchor_x = XAtTime(anchor_seconds);
    px_per_second_ = next;
    scroll_seconds_ = std::max(
        0.0, anchor_seconds -
                 (anchor_x - (timeline_area_.x + kHeaderWidth)) / next);
    Invalidate();
}

void TimelineWidget::ZoomToRange(double t0, double t1) {
    if (!(t1 > t0)) return;
    const double width_px =
        std::max(1.0f, timeline_area_.width - kHeaderWidth);
    px_per_second_ =
        std::clamp(width_px / (t1 - t0), kMinZoom, kMaxZoom);
    scroll_seconds_ = std::max(0.0, t0);
    Invalidate();
}

void TimelineWidget::Tick(double dt_seconds) { (void)dt_seconds; }

bool TimelineWidget::ShowContextMenu(
    const XER::UI::Layout::Point p) {
    namespace Widgets = XER::UI::Widgets;
    if (project_ == nullptr || !timeline_area_.Contains(p)) return false;

    std::vector<Widgets::ContextMenu::Item> items;
    const float lane_bottom = timeline_area_.y + kMarkerLaneHeight;
    const float ruler_bottom = lane_bottom + kRulerHeight;
    const bool in_header = p.x < timeline_area_.x + kHeaderWidth;
    const double t = TimeAtX(p.x);

    if (in_header && p.y >= ruler_bottom) {
        // Track header: state toggles for the track under the cursor.
        const int track = TrackAtY(p.y);
        if (track < 0 ||
            track >= static_cast<int>(project_->tracks.size())) {
            return false;
        }
        const Track& tr = project_->tracks[track];
        if (tr.kind == TrackKind::Audio) {
            Widgets::ContextMenu::Item mute;
            mute.label = "Mute Track";
            mute.checked = tr.muted;
            mute.action = [this, track, on = !tr.muted] {
                if (hooks_.on_track_mute) {
                    hooks_.on_track_mute(track, on);
                }
            };
            items.push_back(std::move(mute));
        } else {
            Widgets::ContextMenu::Item hide;
            hide.label = "Hide Track";
            hide.checked = tr.hidden;
            hide.action = [this, track, on = !tr.hidden] {
                if (hooks_.on_track_hidden) {
                    hooks_.on_track_hidden(track, on);
                }
            };
            items.push_back(std::move(hide));
        }
        Widgets::ContextMenu::Item lock;
        lock.label = "Lock Track";
        lock.checked = tr.locked;
        lock.action = [this, track, on = !tr.locked] {
            if (hooks_.on_track_locked) {
                hooks_.on_track_locked(track, on);
            }
        };
        items.push_back(std::move(lock));
    } else if (in_header) {
        return false;  // ruler/header corner box — no menu
    } else if (p.y < lane_bottom) {
        // Marker lane: clear the marker under the cursor or add one.
        if (const Marker* marker = MarkerAtX(p.x); marker != nullptr) {
            Widgets::ContextMenu::Item clear;
            clear.label = "Clear Marker";
            clear.danger = true;
            clear.action = [this, tm = marker->time] {
                if (hooks_.on_remove_marker) hooks_.on_remove_marker(tm);
            };
            items.push_back(std::move(clear));
        }
        Widgets::ContextMenu::Item add;
        add.label = "Add Marker Here";
        add.separator_before = !items.empty();
        add.action = [this, t] {
            if (hooks_.on_add_marker) hooks_.on_add_marker(t, "");
        };
        items.push_back(std::move(add));
    } else if (p.y < ruler_bottom) {
        // Ruler / work-area bar.
        Widgets::ContextMenu::Item add;
        add.label = "Add Marker Here";
        add.action = [this, t] {
            if (hooks_.on_add_marker) hooks_.on_add_marker(t, "");
        };
        Widgets::ContextMenu::Item win;
        win.label = "Set Work Area In";
        win.separator_before = true;
        win.action = [this, t] {
            if (hooks_.on_set_work_area) {
                hooks_.on_set_work_area(t, project_->work_out);
            }
        };
        Widgets::ContextMenu::Item wout;
        wout.label = "Set Work Area Out";
        wout.action = [this, t] {
            if (hooks_.on_set_work_area) {
                hooks_.on_set_work_area(project_->work_in, t);
            }
        };
        Widgets::ContextMenu::Item reset;
        reset.label = "Reset Work Area";
        reset.enabled =
            project_->work_out > project_->work_in;
        reset.action = [this] {
            if (hooks_.on_set_work_area) hooks_.on_set_work_area(0.0, 0.0);
        };
        items.push_back(std::move(add));
        items.push_back(std::move(win));
        items.push_back(std::move(wout));
        items.push_back(std::move(reset));
    } else {
        // Track lanes: clip menu or an "add marker" fallback on empty space.
        const ClipHit hit = HitClip(p);
        if (hit.clip == nullptr) {
            Widgets::ContextMenu::Item add;
            add.label = "Add Marker at Cursor";
            add.action = [this, t] {
                if (hooks_.on_add_marker) hooks_.on_add_marker(t, "");
            };
            items.push_back(std::move(add));
            // Free clip insertion on the lane under the cursor —
            // Premiere's "New Item -> at playhead", placed at the
            // clicked time instead. Generated kinds only on
            // unlocked video lanes; media import works anywhere
            // (the shell resolves the kind-appropriate target).
            const int lane = TrackAtY(p.y);
            if (lane >= 0 &&
                lane < static_cast<int>(project_->tracks.size()) &&
                !project_->tracks[lane].locked) {
                const Track& lane_track = project_->tracks[lane];
                if (lane_track.kind == TrackKind::Video &&
                    hooks_.on_add_generated) {
                    const struct {
                        const char* label;
                        SourceKind kind;
                    } kInserts[] = {
                        {"Add Color Matte Here", SourceKind::ColorMatte},
                        {"Add Text Here", SourceKind::Text},
                        {"Add Color Bars Here", SourceKind::ColorBars},
                    };
                    bool first = true;
                    for (const auto& ins : kInserts) {
                        Widgets::ContextMenu::Item item;
                        item.label = ins.label;
                        item.separator_before = first;
                        item.action = [this, lane, kind = ins.kind, t] {
                            hooks_.on_add_generated(lane, kind, t);
                        };
                        items.push_back(std::move(item));
                        first = false;
                    }
                }
                if (hooks_.on_add_media) {
                    Widgets::ContextMenu::Item media;
                    media.label = "Add Media Here";
                    media.separator_before = true;
                    media.action = [this, lane, t] {
                        hooks_.on_add_media(lane, t);
                    };
                    items.push_back(std::move(media));
                }
            }
        } else {
            const Clip& clip = *hit.clip;
            if (hooks_.on_select_clip) hooks_.on_select_clip(clip.id);
            const std::string id = clip.id;

            Widgets::ContextMenu::Item split;
            split.label = "Split at Cursor";
            split.shortcut = "C";
            split.enabled = t > clip.timeline_start &&
                            t < clip.timeline_start + clip.duration;
            split.action = [this, id, t] {
                if (hooks_.on_split_clip) hooks_.on_split_clip(id, t);
            };
            items.push_back(std::move(split));

            // Junction pick: right-click on a clip's out-edge offers
            // every supported transition type to the next clip (the
            // shell resolves the adjacent clip and supplies the
            // default duration for a <= 0 request — same contract as
            // Inspector's transition picker).
            if (hit.part == ClipPart::EdgeOut) {
                bool first = true;
                for (const Transition::Type type :
                     AllTransitionTypes()) {
                    if (type == Transition::Type::Script) {
                        continue;   // JS entries fill this slot below
                    }
                    Widgets::ContextMenu::Item trans;
                    trans.label = std::string("Transition: ") +
                                  TransitionTypeLabel(type);
                    trans.separator_before = first;
                    trans.action = [this, id, type] {
                        if (hooks_.on_add_transition) {
                            hooks_.on_add_transition(id, type, -1.0,
                                                     "");
                        }
                    };
                    items.push_back(std::move(trans));
                    first = false;
                }
                for (const auto& desc :
                     JsFxRuntime::ScriptTransitions()) {
                    Widgets::ContextMenu::Item trans;
                    trans.label = "Transition: JS " + desc.title;
                    const std::string script_id = desc.id;
                    trans.action = [this, id, script_id] {
                        if (hooks_.on_add_transition) {
                            hooks_.on_add_transition(
                                id, Transition::Type::Script, -1.0,
                                script_id);
                        }
                    };
                    items.push_back(std::move(trans));
                }
            }

            if (!clip.is_adjustment) {
                for (const double rate : {0.5, 1.0, 2.0}) {
                    Widgets::ContextMenu::Item speed;
                    char buf[32];
                    std::snprintf(buf, sizeof(buf), "Speed %g%%",
                                  rate * 100.0);
                    speed.label = buf;
                    speed.checked =
                        std::abs(clip.playback_rate - rate) < 0.001;
                    speed.action = [this, id, rate] {
                        if (hooks_.on_rate_stretch) {
                            hooks_.on_rate_stretch(id, rate);
                        }
                    };
                    if (rate == 0.5) speed.separator_before = true;
                    items.push_back(std::move(speed));
                }
            }

            Widgets::ContextMenu::Item del;
            del.label = "Delete";
            del.shortcut = "Del";
            del.danger = true;
            del.separator_before = true;
            del.action = [this, id] {
                if (hooks_.on_delete_clip) hooks_.on_delete_clip(id);
            };
            Widgets::ContextMenu::Item ripple;
            ripple.label = "Ripple Delete";
            ripple.danger = true;
            ripple.action = [this, id] {
                if (hooks_.on_ripple_delete) hooks_.on_ripple_delete(id);
            };
            items.push_back(std::move(del));
            items.push_back(std::move(ripple));
        }
    }

    if (items.empty()) return false;
    auto menu = Widgets::ContextMenu::Create(std::move(items));
    menu->SetAnchor(p);
    ShowOverlay(menu);
    return true;
}

}  // namespace XER::VideoEditor::Ui
