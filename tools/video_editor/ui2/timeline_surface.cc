// TimelineSurface — S1 timeline surface (spec §6). Ported from
// ui/timeline_widget.cc: same geometry constants, paint order,
// hit zones, caches and gesture dispatch. Differences from the
// legacy widget (kept intentional):
//   * Hooks type is Ui2::TimelineHooks (surface_hooks.h).
//   * Tools live in ui2/timeline_tools.{h,cc} (TimelineTool).
//   * Icon-pack rasterization (Ui2::VeIcons) is not wired — ui2 draws
//     the same hand-vector glyphs the old code used as its headless
//     fallback, keeping the surface self-contained.
//   * JS script-transition submenu entries are left to the bridge
//     (on_add_transition already carries Type::Script + script_id —
//     the shell can list JsFxRuntime items when it wires the seam).
//   * Gesture bracketing: pointer dispatch wraps tool commits in the
//     optional on_gesture_begin/on_gesture_end hooks so the bridge
//     lands every burst (linked co-move, ripple trim) as ONE history
//     macro — the contract's documented shell-side requirement.

#include "timeline_surface.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
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
#include "media_catalog.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "transitions_lib.h"
#include "ve_theme.h"
#include "widgets/context_menu.h"

namespace XER::VideoEditor::Ui2 {
namespace {

using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

// ---- layout metrics (VeTheme 4px grid — old widget constants) ----
constexpr float kHeaderWidth = 172.0f;    // track-header column
constexpr float kMarkerLaneHeight = 18.0f;
constexpr float kRulerHeight = 28.0f;
constexpr float kWorkBarHeight = 10.0f;   // band at the ruler's top edge
constexpr float kTrackHeightExpanded = 116.0f;
constexpr float kTrackGap = 3.0f;
constexpr float kClipInset = 4.0f;
constexpr float kEdgeGrabPx = 7.0f;
constexpr float kHeaderButtonSize = 18.0f;
constexpr float kHeaderButtonStep = 22.0f;
constexpr float kClipLabelHeight = 17.0f; // name strip at a clip's top
constexpr float kClipRadius = 5.0f;
constexpr float kKindBarWidth = 3.0f;     // colored bar on header's left
constexpr float kToolButtonSize = 20.0f;
constexpr float kToolButtonStep = 26.0f;
constexpr int kToolButtonCount = 4;       // magnet, razor, marker, fx
constexpr double kMinZoom = 8.0;
constexpr double kMaxZoom = 600.0;
constexpr std::chrono::milliseconds kDoubleClick{450};
constexpr std::size_t kMaxWaveRows = 24;   // downsampled-waveform LRU
constexpr std::size_t kMaxPosters = 160;   // poster-frame LRU
constexpr std::size_t kMaxPeaks = 32;      // decoded AudioPeaks
constexpr std::size_t kMaxMediaDurations = 512;
constexpr std::size_t kMaxMissingKeys = 1024;
constexpr double kPosterQuantize = 0.25;   // poster cache quantum (s)
constexpr double kFallbackSampleRate = 48000.0;

constexpr Color kAccentCyan{0.13f, 0.83f, 0.93f, 1.0f};      // #22d3ee

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

// Saturated clip-TYPE accent for the card's header strip —
// CapCut-style type coding: media on video lanes reads blue, audio
// lanes teal (per-track hue alternation via Theme::ClipBaseColor),
// title/text clips violet, generated sources (matte/bars) slate and
// adjustment layers steel.
Color ClipTypeColor(const Clip& clip, const Track& track) {
    const bool video = track.kind == TrackKind::Video;
    if (video && clip.is_adjustment) return Theme::kClipAdjustment;
    if (video && clip.source.kind != SourceKind::Media) {
        return clip.source.kind == SourceKind::Text
                   ? Theme::kClipTitle
                   : Theme::kClipGenerated;
    }
    return Theme::ClipBaseColor(
        video, static_cast<std::size_t>(clip.track));
}

// Track-kind badge accent (header kind bar, badge chip, row wash).
Color KindColor(const XER::UI::Theme::Tokens& tokens,
                bool video_track) {
    return video_track ? tokens.track_video : tokens.track_audio;
}

// Header affordance ids (see TimelineSurface::HeaderHit):
//   0 = mute (audio), 1 = eye/hidden (video), 2 = lock,
//   3 = expand chevron, 4 = solo (audio), 5 = record arm (audio).
// Kind gate mirroring ShowContextMenu's track menu: mute/solo/arm are
// audio-track concepts, the hidden/eye a video-track one; lock and
// the expand chevron apply to both.
bool HeaderButtonApplies(const Track& track, int button) {
    switch (button) {
        case 0:                                          // M = muted
        case 4:                                          // S = solo
        case 5:                                          // mic = arm
            return track.kind == TrackKind::Audio;
        case 1: return track.kind == TrackKind::Video;   // eye
        default: return true;                            // lock/chev
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
    return Rect{label_rect.x + label_rect.width - Theme::kSpaceSm -
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
// The ui2 surface draws the hand-vector glyphs directly (the legacy
// widget's headless fallback) so it has no IconPack dependency.

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
    renderer.DrawLine({cx - 4.6f, top + 7.0f}, {cx - 4.6f, top + 9.6f},
                      color, 1.3f);
    renderer.DrawLine({cx - 4.6f, top + 9.6f}, {cx + 4.6f, top + 9.6f},
                      color, 1.3f);
    renderer.DrawLine({cx + 4.6f, top + 9.6f}, {cx + 4.6f, top + 7.0f},
                      color, 1.3f);
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

TimelineSurface::TimelineSurface(TimelineHooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);
    // One instance per tool — the active id picks the receiver of
    // pointer events (SetTool never allocates on the hot path).
    static constexpr ToolId kAllTools[] = {
        ToolId::Selection,   ToolId::Razor,  ToolId::RippleEdit,
        ToolId::RollingEdit, ToolId::Slip,   ToolId::Slide,
        ToolId::RateStretch, ToolId::Hand,   ToolId::Zoom,
    };
    for (ToolId id : kAllTools) {
        tools_[id] = MakeTimelineTool(id);
    }
}

void TimelineSurface::SetHooks(TimelineHooks hooks) {
    hooks_ = std::move(hooks);
    // Media caches key on hook-served data — a different provider
    // invalidates every cached answer.
    peaks_cache_.clear();
    peaks_lru_.clear();
    peaks_missing_.clear();
    peaks_missing_lru_.clear();
    wave_cache_.clear();
    wave_lru_.clear();
    media_duration_cache_.clear();
    duration_lru_.clear();
    poster_cache_.clear();
    poster_lru_.clear();
    poster_missing_.clear();
    poster_missing_lru_.clear();
    Invalidate();
}

void TimelineSurface::SetProject(const Project* project) {
    project_ = project;
    // The project object may have been rebuilt by a commit — drop any
    // stale tool previews keyed by old clip ids.
    clip_previews_.clear();
    Invalidate();
}

void TimelineSurface::SetPlayheadSeconds(double seconds) {
    const double prev_playhead = playhead_;
    const double prev_scroll = scroll_seconds_;
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
    if (scroll_seconds_ != prev_scroll) {
        // Auto-scroll moved the content — the whole viewport repaints.
        Invalidate();
        return;
    }
    if (playhead_ == prev_playhead) {
        return;
    }
    // Only the old and new playhead columns changed: the cap handle
    // is 14px wide and the line is 2px, so a 16px column covers both
    // (the damage tracker inflates beyond that anyway).
    const Rect b = GetBounds();
    const auto strip = [&](double t) {
        return Rect{XAtTime(t) - 8.0f, b.y, 16.0f, b.height};
    };
    Invalidate(strip(prev_playhead));
    Invalidate(strip(playhead_));
}

void TimelineSurface::SetSelectedClip(const std::string& clip_id) {
    selected_clip_id_ = clip_id;
    Invalidate();
}

void TimelineSurface::SetSnapEnabled(bool enabled) {
    snap_enabled_ = enabled;
    Invalidate();
}

void TimelineSurface::SetZoom(double px_per_second) {
    px_per_second_ = std::clamp(px_per_second, kMinZoom, kMaxZoom);
    Invalidate();
}

void TimelineSurface::SetScrollSeconds(double seconds) {
    scroll_seconds_ = std::max(0.0, seconds);
    Invalidate();
}

void TimelineSurface::SetTrackHeight(float height_px) {
    track_height_ = std::clamp(height_px, 36.0f, 140.0f);
    Invalidate();
}

void TimelineSurface::SetLinkEditsEnabled(bool enabled) {
    link_edits_enabled_ = enabled;
}

void TimelineSurface::ZoomToFit() {
    const double duration =
        project_ != nullptr ? project_->duration() : 0.0;
    if (duration <= 0.0) return;
    ZoomToRange(0.0, duration);
}

void TimelineSurface::SetTool(ToolId tool) {
    if (tool == active_tool_) return;
    transition_drag_ = false;  // junction-handle drags die with the tool
    if (TimelineTool* current = ActiveToolImpl()) {
        current->Cancel(*this);
    }
    active_tool_ = tool;
    if (TimelineTool* next = ActiveToolImpl()) {
        SetStatus(std::string("Tool: ") + next->Name() + " — " +
                  next->CursorHint());
    }
    if (on_tool_changed_) on_tool_changed_(tool);
    Invalidate();
}

const char* TimelineSurface::ActiveToolName() const {
    const TimelineTool* tool = ActiveToolImpl();
    return tool != nullptr ? tool->Name() : "?";
}

const char* TimelineSurface::ActiveToolCursorHint() const {
    const TimelineTool* tool = ActiveToolImpl();
    return tool != nullptr ? tool->CursorHint() : "";
}

void TimelineSurface::SetOnToolChanged(
    std::function<void(ToolId)> callback) {
    on_tool_changed_ = std::move(callback);
}

TimelineTool* TimelineSurface::ActiveToolImpl() const {
    const auto it = tools_.find(active_tool_);
    return it != tools_.end() ? it->second.get() : nullptr;
}

double TimelineSurface::TimeAtDropPoint(
    XER::UI::Layout::Point point) const {
    return TimeAtX(point.x);
}

int TimelineSurface::TrackAtDropPoint(
    XER::UI::Layout::Point point) const {
    return TrackAtY(point.y);
}

void TimelineSurface::SetExternalDropHover(bool active) {
    if (external_drop_hover_ == active) return;
    external_drop_hover_ = active;
    Invalidate();
}

double TimelineSurface::TimeAtX(float x) const {
    const float local = x - (timeline_area_.x + kHeaderWidth);
    return std::max(0.0, scroll_seconds_ + local / px_per_second_);
}

float TimelineSurface::XAtTime(double t) const {
    return timeline_area_.x + kHeaderWidth +
           static_cast<float>((t - scroll_seconds_) * px_per_second_);
}

float TimelineSurface::TrackRowHeight(int index) const {
    return expanded_tracks_.count(index) != 0u
               ? std::max(kTrackHeightExpanded, track_height_ + 52.0f)
               : track_height_;
}

int TimelineSurface::TrackAtY(float y) const {
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

Rect TimelineSurface::TrackRect(int index) const {
    float y = timeline_area_.y + kMarkerLaneHeight + kRulerHeight -
              track_scroll_;
    for (int i = 0; i < index &&
            i < static_cast<int>(project_ != nullptr
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
Rect TimelineSurface::ToolbarButtonRect(int index) const {
    return Rect{timeline_area_.x + Theme::kSpaceMd +
                    static_cast<float>(index) * kToolButtonStep,
                timeline_area_.y + 24.0f, kToolButtonSize,
                kToolButtonSize};
}

int TimelineSurface::ToolbarButtonAt(Point p) const {
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

Rect TimelineSurface::HeaderRect(int index) const {
    const Rect row = TrackRect(index);
    return Rect{timeline_area_.x + Theme::kSpaceXs, row.y,
                kHeaderWidth - Theme::kSpaceXs * 2.0f, row.height};
}

Rect TimelineSurface::RealClipRect(const Clip& clip) const {
    const Rect track = TrackRect(clip.track);
    const float x0 = XAtTime(clip.timeline_start);
    const float x1 = XAtTime(ClipEndSeconds(clip));
    return Rect{x0, track.y + kClipInset,
                std::max(3.0f, x1 - x0),
                track.height - kClipInset * 2.0f};
}

Rect TimelineSurface::ClipRect(const Clip& clip) const {
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

TimelineSurface::ClipHit TimelineSurface::HitClip(Point p) const {
    if (project_ == nullptr ||
        p.x < timeline_area_.x + kHeaderWidth) {
        return {};
    }
    for (auto it = project_->clips.rbegin();
         it != project_->clips.rend(); ++it) {
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

TimelineSurface::HeaderHit TimelineSurface::HitHeaderButton(
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

const Marker* TimelineSurface::MarkerAtX(float x) const {
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

int TimelineSurface::TopZoneAt(Point p) const {
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

double TimelineSurface::EffectiveTransitionDuration(
    const Transition& transition) const {
    if (transition_drag_ &&
        transition.from_clip == transition_drag_from_ &&
        transition.to_clip == transition_drag_to_) {
        return transition_drag_preview_;
    }
    return transition.duration;
}

double TimelineSurface::TransitionDragLimit(
    const Transition& transition) const {
    const Clip* from = FindClip(*project_, transition.from_clip);
    const Clip* to = FindClip(*project_, transition.to_clip);
    if (from == nullptr || to == nullptr) return 0.0;
    auto bounds_for = [this](const Clip* clip) {
        MediaBounds bounds;   // duration_seconds 0 = unknown/unbounded
        if (clip->source.kind == SourceKind::Media &&
            !clip->source_path.empty() && hooks_.get_media_bounds) {
            hooks_.get_media_bounds(clip->source_path, &bounds);
        }
        return bounds;
    };
    return TransitionDurationLimit(*project_, transition,
                                   bounds_for(from), bounds_for(to));
}

TimelineSurface::TransitionHandleHit
TimelineSurface::HitTransitionHandle(Point p) const {
    TransitionHandleHit hit;
    if (project_ == nullptr ||
        p.x < timeline_area_.x + kHeaderWidth) {
        return hit;
    }
    constexpr float kEdgeZone = 4.0f;   // ~4px grab each side of a bar
    for (std::size_t i = 0; i < project_->transitions.size(); ++i) {
        const Transition& transition = project_->transitions[i];
        const Clip* from = FindClip(*project_, transition.from_clip);
        const Clip* to = FindClip(*project_, transition.to_clip);
        if (from == nullptr || to == nullptr) continue;
        if (from->track < 0 ||
            from->track >= static_cast<int>(project_->tracks.size())) {
            continue;
        }
        // Same region math the junction overlay renders.
        const double cut = ClipEndSeconds(*from);
        const double half =
            std::max(0.05, EffectiveTransitionDuration(transition)) *
            0.5;
        const float xc = XAtTime(cut);
        float x0 = XAtTime(cut - half);
        float x1 = XAtTime(cut + half);
        const Rect row = TrackRect(from->track);
        const float top = row.y + kClipInset + 1.0f;
        const float bottom = row.y + row.height - kClipInset - 1.0f;
        if (bottom <= top || p.y < top || p.y >= bottom) continue;
        if (x1 - x0 < 12.0f) {
            x0 = xc - 6.0f;
            x1 = xc + 6.0f;
        }
        const float dl = std::fabs(p.x - x0);
        const float dr = std::fabs(p.x - x1);
        if (dl <= kEdgeZone && dl <= dr) {
            hit.index = static_cast<int>(i);
            hit.edge = TrimEdge::In;
        } else if (dr <= kEdgeZone) {
            hit.index = static_cast<int>(i);
            hit.edge = TrimEdge::Out;
        }
        if (hit.index >= 0) return hit;
    }
    return hit;
}

ToolContext TimelineSurface::MakeContext(
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
    ctx.link_edits = link_edits_enabled_;
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

void TimelineSurface::SetStatus(const std::string& text,
                                bool is_error) {
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

const AudioPeaks* TimelineSurface::PeaksFor(const std::string& path) {
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
            // read out of bounds. Division form: a forged giant
            // `buckets` cannot wrap the product back under
            // data.size().
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
            TrackMissing(peaks_missing_, peaks_missing_lru_, path,
                         kMaxMissingKeys);
            return nullptr;
    }
    return nullptr;
}

std::size_t TimelineSurface::PrimeMediaLookups() {
    if (project_ == nullptr) return 0;
    std::size_t probed = 0;
    std::unordered_set<std::string> seen;
    for (const Clip& clip : project_->clips) {
        if (clip.source_path.empty() ||
            !seen.insert(clip.source_path).second) {
            continue;
        }
        ++probed;
        // kPending answers drop straight through — none of these
        // block on decode, they only seed caches/queues.
        (void)PeaksFor(clip.source_path);
        (void)MediaDurationFor(clip.source_path);
        (void)PosterFor(clip.source_path, clip.in_point);
    }
    return probed;
}

void TimelineSurface::NotifyMediaChanged() {
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
    Invalidate();
}

double TimelineSurface::MediaDurationFor(const std::string& path) {
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

const TimelineSurface::WaveRow* TimelineSurface::WaveFor(
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
    const std::string key = path + "@" + std::to_string(columns);
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

const video::Frame* TimelineSurface::PosterFor(const std::string& path,
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

const video::Frame* TimelineSurface::NearestPoster(
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

void TimelineSurface::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    const Rect bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_large);
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
    const Rect caption{bounds.x + Theme::kSpaceMd, bounds.y + 3.0f,
                       kHeaderWidth - Theme::kSpaceMd * 2.0f, 14.0f};
    renderer.DrawText("TIMELINE", caption,
                      {tokens.muted, tokens.font_small, 700});
    if (const TimelineTool* tool = ActiveToolImpl()) {
        const std::string tool_name = tool->Name();
        const auto name_size = renderer.MeasureText(
            tool_name, {kAccentCyan, tokens.font_small, 600});
        const auto caption_size = renderer.MeasureText(
            "TIMELINE", {tokens.muted, tokens.font_small, 700});
        const Rect chip{caption.x + caption_size.width + 8.0f,
                        bounds.y + 2.0f, name_size.width + 10.0f,
                        15.0f};
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
                          WithAlpha(tokens.timecode, 0.7f), 1.0f);
        // Monospaced cyan timecode marks (reference ruler style),
        // 4px right of the tick.
        char label[32];
        if (step < 1.0) {
            std::snprintf(label, sizeof(label), "%.2fs", tick);
        } else {
            const int minutes = static_cast<int>(tick / 60.0);
            const int seconds =
                static_cast<int>(std::fmod(tick, 60.0));
            std::snprintf(label, sizeof(label), "%d:%02d", minutes,
                          seconds);
        }
        renderer.DrawText(label,
                          Rect{x + 4.0f,
                               lane_bottom + kWorkBarHeight + 1.0f,
                               56.0f, 14.0f},
                          {tokens.timecode, tokens.font_small, 500,
                           true});
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
    // clip the whole band (header column and track area alike) to
    // the region below the ruler.
    renderer.PushClip({timeline_area_.x, ruler_bottom,
                       timeline_area_.width,
                       std::max(0.0f, bounds.y + bounds.height -
                                          ruler_bottom)});
    for (std::size_t i = 0; i < project_->tracks.size(); ++i) {
        const Track& track = project_->tracks[i];
        const bool is_video = track.kind == TrackKind::Video;
        const Color kind = KindColor(tokens, is_video);
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
        // (duplicate of the badge id is hidden); state flags below.
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
        const float flags_y = label_rect.y + 24.0f;
        renderer.DrawText(
            flags,
            Rect{name_x, flags_y,
                 label_rect.x + label_rect.width - name_x - 6.0f,
                 15.0f},
            {flags.front() == 'v' || flags.front() == 'a'
                 ? tokens.disabled
                 : tokens.danger,
             tokens.font_small, 400});
        RenderHeaderButtons(renderer, tokens, track,
                            static_cast<int>(i), label_rect);

        // Expand chevron at the header's top-right corner — swaps
        // the lane between compact and tall (waveform/thumbnail
        // room).
        const bool expanded = expanded_tracks_.count(i) != 0u;
        const Rect chev = HeaderChevronRect(label_rect);
        const bool chev_hot =
            hover_header_.track == static_cast<int>(i) &&
            hover_header_.button == 3;
        if (expanded || chev_hot) {
            renderer.FillRect(chev, tokens.hover, 4.0f);
        }
        DrawChevronIcon(renderer, chev,
                        expanded ? kAccentCyan : tokens.muted,
                        expanded);

        // Track lane: sunken fill with a whisper of the kind tint.
        Color row_color =
            Theme::Mix(tokens.input, kind, is_video ? 0.05f : 0.07f);
        if (track.locked) {
            row_color = WithAlpha(row_color, 0.6f);
        }
        renderer.FillRect(row, row_color, tokens.radius_small);
    }
    renderer.PopClip();   // rows/header scroll band

    // ---- clips ----
    // Clips render inside the track-area viewport only — a clip that
    // starts before the scroll offset must not bleed under the
    // header column.
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
        const Color type = ClipTypeColor(clip, track);
        // Card body: a dark shade of the type accent — poster frames
        // and waveform peaks stay readable over it while the card
        // keeps its hue end to end. The resting border is the accent
        // at half strength.
        const Color card = Theme::Mix(type, tokens.input, 0.60f);
        const Color edge = WithAlpha(type, 0.50f);
        const bool previewed = clip_previews_.count(clip.id) != 0u;
        if (previewed) {
            // Original position stays as a dim ghost under the
            // previewed span (Premiere's edit preview).
            const Rect real = RealClipRect(clip);
            renderer.FillRect(real, WithAlpha(type, 0.14f),
                              kClipRadius);
            renderer.StrokeRect(real, WithAlpha(tokens.border, 0.5f),
                                1.0f, kClipRadius);
        }
        float alpha = clip.muted || track.hidden ? 0.30f : 1.0f;
        if (track.locked) alpha *= 0.5f;   // locked tracks dim
        renderer.FillRect(rect, WithAlpha(card, alpha), kClipRadius);
        const Rect strip{rect.x, rect.y, rect.width,
                         std::min(kClipLabelHeight, rect.height)};
        renderer.FillRect(strip, WithAlpha(type, alpha), kClipRadius);
        if (strip.height > kClipRadius) {
            renderer.FillRect(
                Rect{strip.x, strip.y + strip.height - kClipRadius,
                     strip.width, kClipRadius},
                WithAlpha(type, alpha), 0.0f);
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
                                  : edge;
        renderer.StrokeRect(
            rect, stroke,
            previewed || selected ? 2.0f : 1.0f, kClipRadius);

        // Content region below the label strip, clipped to the clip
        // rect (waveforms/posters stay inside the rounded bounds).
        const Rect content{rect.x + 1.0f,
                           rect.y + kClipLabelHeight + 1.0f,
                           std::max(0.0f, rect.width - 2.0f),
                           std::max(0.0f, rect.height -
                                              kClipLabelHeight -
                                              2.0f)};
        renderer.PushClip(content);
        if (track.kind == TrackKind::Audio) {
            RenderWaveform(renderer, tokens, clip, content);
        } else {
            RenderThumbnails(renderer, tokens, clip, content);
        }
        renderer.PopClip();

        // Clip name on the accent strip — 6px left inset, vertically
        // centered. Light strips take dark text for contrast.
        // Right-edge chips stack inward: "lock" tag, amber "fx",
        // link rings.
        const float type_lum =
            type.r * 0.299f + type.g * 0.587f + type.b * 0.114f;
        const Color label_on =
            type_lum > 0.60f
                ? Color{0.08f, 0.09f, 0.11f, 0.95f}
                : Color{1.0f, 1.0f, 1.0f, 0.96f};
        const Color label_color =
            track.locked || clip.muted || track.hidden
                ? WithAlpha(label_on, 0.62f)
                : label_on;
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
            // Amber "fx" chip — clip carries an effect/audio-fx
            // chain.
            const Rect fx{right_cursor - 18.0f, rect.y + 3.0f, 18.0f,
                          12.0f};
            renderer.FillRect(
                fx, WithAlpha(tokens.warning_chip, 0.92f), 3.0f);
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
        // Duration/rate sub-line under the strip, left-aligned with
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
            {WithAlpha(Color{1, 1, 1, 1}, alpha * 0.72f),
             tokens.font_small, 400});

        // AI tag ticks — ClipTag ranges are SOURCE seconds; the
        // visible window is [in_point, in_point + duration*rate)
        // mapped onto the clip rect, so the ticks follow trims and
        // rate changes like the thumbnails do.
        if (!clip.tags.empty() && rect.height > 22.0f) {
            const double rate =
                clip.playback_rate > 0.0 ? clip.playback_rate : 1.0;
            const double src_end =
                clip.in_point + clip.duration * rate;
            const double inv =
                rect.width /
                (clip.duration > 0.0 ? clip.duration : 1.0);
            for (const ClipTag& tag : clip.tags) {
                const double lo = std::max(tag.start, clip.in_point);
                const double hi = std::min(tag.end, src_end);
                if (hi <= lo) continue;
                const float x0 =
                    rect.x +
                    static_cast<float>((lo - clip.in_point) / rate *
                                       inv);
                const float x1 =
                    rect.x +
                    static_cast<float>((hi - clip.in_point) / rate *
                                       inv);
                if (x1 - x0 < 1.5f) continue;
                // Stable per-label hue from a small palette so the
                // same class keeps its color across the strip.
                static constexpr Color kTagPalette[] = {
                    {0.95f, 0.75f, 0.10f, 1.0f},
                    {0.35f, 0.85f, 0.55f, 1.0f},
                    {0.45f, 0.65f, 0.95f, 1.0f},
                    {0.90f, 0.50f, 0.80f, 1.0f},
                    {0.95f, 0.55f, 0.30f, 1.0f},
                };
                std::uint32_t h = 5381u;
                for (const unsigned char c : tag.label) {
                    h = h * 33u + c;
                }
                const Color tick_color =
                    kTagPalette[h % std::size(kTagPalette)];
                renderer.FillRect(
                    Rect{x0, rect.y + rect.height - 4.0f,
                         x1 - x0, 3.0f},
                    WithAlpha(tick_color, alpha * 0.85f), 1.5f);
            }
        }

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

    // Transition overlays at cuts — a hatched accent band spanning
    // the whole transition window (the model centers `duration` on
    // the junction) with bright edge bars that read as duration
    // handles and a centered glyph chip.
    for (std::size_t ti = 0; ti < project_->transitions.size(); ++ti) {
        const Transition& transition = project_->transitions[ti];
        const Clip* from = FindClip(*project_, transition.from_clip);
        const Clip* to = FindClip(*project_, transition.to_clip);
        if (from == nullptr || to == nullptr) continue;
        if (from->track < 0 ||
            from->track >= static_cast<int>(project_->tracks.size())) {
            continue;
        }
        // A handle drag previews its duration live — the band and
        // the duration text resize while the model is untouched
        // until the mouse-up commit.
        const double duration =
            EffectiveTransitionDuration(transition);
        const double cut = ClipEndSeconds(*from);
        const double half = std::max(0.05, duration) * 0.5;
        const float xc = XAtTime(cut);
        float x0 = XAtTime(cut - half);
        float x1 = XAtTime(cut + half);
        const Rect row = TrackRect(from->track);
        const float top = row.y + kClipInset + 1.0f;
        const float bottom =
            row.y + row.height - kClipInset - 1.0f;
        if (bottom <= top) continue;
        // Degenerate zooms collapse the window to a sliver — keep a
        // minimum readable span for the indicator.
        if (x1 - x0 < 12.0f) {
            x0 = xc - 6.0f;
            x1 = xc + 6.0f;
        }
        const Rect region{x0, top, x1 - x0, bottom - top};
        const Color tint = tokens.accent;
        renderer.FillRect(region, WithAlpha(tint, 0.16f), 3.0f);
        // Diagonal hatch inside the region — the "blend zone" read.
        renderer.PushClip(region);
        for (float hx = x0 - region.height; hx < x1; hx += 7.0f) {
            renderer.DrawLine({hx, bottom},
                              {hx + region.height, top},
                              WithAlpha(tint, 0.14f), 1.0f);
        }
        renderer.PopClip();
        // Duration handles — bright bars at both region edges, and
        // the actual drag zones (HitTransitionHandle). Hover
        // widens/fills the edge under the pointer; an active drag
        // hots both edges since the centered window moves them
        // together.
        const bool dragging_this =
            transition_drag_ &&
            transition.from_clip == transition_drag_from_ &&
            transition.to_clip == transition_drag_to_;
        const bool hot_l =
            dragging_this ||
            (hover_transition_ == static_cast<int>(ti) &&
             hover_transition_edge_ == TrimEdge::In);
        const bool hot_r =
            dragging_this ||
            (hover_transition_ == static_cast<int>(ti) &&
             hover_transition_edge_ == TrimEdge::Out);
        renderer.FillRect(
            Rect{x0 - (hot_l ? 2.0f : 1.5f), top,
                 hot_l ? 4.0f : 3.0f, bottom - top},
            WithAlpha(tint, hot_l ? 1.0f : 0.85f), 1.5f);
        renderer.FillRect(
            Rect{x1 - (hot_r ? 2.0f : 1.5f), top,
                 hot_r ? 4.0f : 3.0f, bottom - top},
            WithAlpha(tint, hot_r ? 1.0f : 0.85f), 1.5f);
        // Centered indicator chip: dark pill with two inward
        // triangles (dissolve glyph); the duration text joins when
        // the region is wide enough to carry it.
        char dur[16];
        std::snprintf(dur, sizeof(dur), "%.1fs", duration);
        const auto dur_size = renderer.MeasureText(
            dur, {tint, tokens.font_small, 600});
        const bool show_dur =
            region.width >= dur_size.width + 36.0f;
        const float chip_w =
            show_dur ? dur_size.width + 30.0f : 20.0f;
        const Rect chip{xc - chip_w * 0.5f,
                        top + (bottom - top) * 0.5f - 7.0f, chip_w,
                        14.0f};
        renderer.FillRect(chip, Theme::kOverlayChip, 7.0f);
        renderer.StrokeRect(chip, WithAlpha(tint, 0.7f), 1.0f, 7.0f);
        const float gcy = chip.y + chip.height * 0.5f;
        const float gx = show_dur ? chip.x + 10.0f
                                  : chip.x + chip.width * 0.5f;
        renderer.DrawLine({gx - 4.0f, gcy - 3.5f}, {gx - 0.5f, gcy},
                          tint, 1.3f);
        renderer.DrawLine({gx - 0.5f, gcy}, {gx - 4.0f, gcy + 3.5f},
                          tint, 1.3f);
        renderer.DrawLine({gx + 4.0f, gcy - 3.5f}, {gx + 0.5f, gcy},
                          tint, 1.3f);
        renderer.DrawLine({gx + 0.5f, gcy}, {gx + 4.0f, gcy + 3.5f},
                          tint, 1.3f);
        if (show_dur) {
            renderer.DrawText(
                dur,
                Rect{chip.x + 19.0f, chip.y + 0.5f,
                     std::max(0.0f, chip.width - 21.0f),
                     chip.height - 1.0f},
                {tint, tokens.font_small, 600});
        }
    }
    renderer.PopClip();   // tracks_viewport

    // Empty-state hint: a centered muted line in the track area when
    // the timeline holds no clips — plain dark lanes otherwise read
    // as a rendering dead zone, not a destination.
    if (project_ != nullptr && project_->clips.empty() &&
        tracks_viewport.width > 120.0f && tracks_viewport.height > 30.0f) {
        constexpr std::string_view hint = "Drag media here to start editing";
        const UI::Rendering::TextStyle hint_style{tokens.muted,
                                                  tokens.font_small};
        const auto hs = renderer.MeasureText(hint, hint_style);
        renderer.DrawText(
            hint,
            Rect{tracks_viewport.x +
                     std::max(0.0f, (tracks_viewport.width - hs.width) *
                                        0.5f),
                 tracks_viewport.y +
                     std::max(0.0f, (tracks_viewport.height - hs.height) *
                                        0.5f),
                 std::min(hs.width, tracks_viewport.width),
                 std::min(hs.height, tracks_viewport.height)},
            hint_style);
    }

    // Zoom marquee (rubber band) across the full track area.
    if (marquee_active_ && marquee_x1_ > marquee_x0_) {
        const Rect band{marquee_x0_, ruler_bottom,
                        marquee_x1_ - marquee_x0_,
                        bounds.y + bounds.height - ruler_bottom};
        renderer.FillRect(band, WithAlpha(tokens.accent, 0.12f), 0.0f);
        renderer.StrokeRect(band, tokens.accent, 1.0f, 0.0f);
    }

    // Snap guide: while a drag's SnapCandidate resolved to a
    // different time, a full-height accent line marks the target
    // (Premiere's snap indicator).
    if (snap_indicator_t_ >= 0.0) {
        const float sx = XAtTime(snap_indicator_t_);
        renderer.DrawLine({sx, ruler_bottom},
                          {sx, bounds.y + bounds.height},
                          kAccentCyan, 1.5f);
    }

    // Playhead: bright cyan line with a pill-shaped cap handle in
    // the ruler/marker band (reference NLE style).
    const float play_x = XAtTime(playhead_);
    renderer.FillRect(
        Rect{play_x - 7.0f, bounds.y + 1.0f, 14.0f, 14.0f},
        tokens.playhead, 7.0f);
    renderer.DrawLine({play_x, bounds.y + 5.0f},
                      {play_x, bounds.y + bounds.height},
                      tokens.playhead, 2.0f);

    // Media-bin drag hovering this surface — accent frame marks the
    // panel as a live drop target (SetExternalDropHover).
    if (external_drop_hover_) {
        renderer.StrokeRect(bounds, tokens.accent, 2.0f,
                            tokens.radius_large);
    }
}

void TimelineSurface::RenderMarkers(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Rect& bounds) {
    const Rect lane{timeline_area_.x + kHeaderWidth, bounds.y,
                    std::max(0.0f, bounds.width - kHeaderWidth),
                    kMarkerLaneHeight};
    renderer.FillRect(lane, WithAlpha(tokens.background, 0.55f), 0.0f);
    renderer.DrawLine(
        {lane.x, bounds.y + kMarkerLaneHeight},
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

void TimelineSurface::RenderWorkArea(
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

void TimelineSurface::RenderHeaderButtons(
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
    const Color kind =
        KindColor(tokens, track.kind == TrackKind::Video);
    const int count = HeaderButtonCount(track);
    for (int slot = 0; slot < count; ++slot) {
        const int button = HeaderButtonIdFor(track, slot);
        const Rect r = HeaderButtonRect(label_rect, slot, count);
        const bool active = is_active(button);
        const bool hot = hover_header_.track == track_index &&
                         hover_header_.button == button;
        // Ghost affordance: no chrome until hovered; active toggles
        // get a tinted chip (lock reads danger, kind toggles the
        // kind color).
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
            case 0:   // mute — letter chip fallback glyph
                DrawCenteredText(
                    renderer, "M",
                    Rect{r.x, r.y + 1.5f, r.width, r.height},
                    {glyph, tokens.font_small, 700});
                break;
            case 4:   // solo
                DrawCenteredText(
                    renderer, "S",
                    Rect{r.x, r.y + 1.5f, r.width, r.height},
                    {glyph, tokens.font_small, 700});
                break;
            case 5:   // record arm
                DrawMicIcon(renderer, box, glyph);
                break;
            case 1:   // visibility (slashed eye when hidden)
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

void TimelineSurface::RenderToolbar(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Rect& bounds) {
    (void)bounds;
    // Ghost icon row under the caption: magnet (snap), razor, marker
    // at playhead, fx badge. No chrome until hovered, tinted chip
    // while the state is on.
    for (int i = 0; i < kToolButtonCount; ++i) {
        const Rect r = ToolbarButtonRect(i);
        const bool hot = hover_toolbar_ == i;
        const bool active =
            (i == 0 && snap_enabled_) ||
            (i == 1 && active_tool_ == ToolId::Razor);
        if (active) {
            renderer.FillRect(r, WithAlpha(kAccentCyan, 0.18f),
                              tokens.radius_small);
            renderer.StrokeRect(r, WithAlpha(kAccentCyan, 0.55f),
                                1.0f, tokens.radius_small);
        } else if (hot) {
            renderer.FillRect(r, tokens.hover, tokens.radius_small);
        }
        const Color glyph =
            active ? kAccentCyan
                   : (hot ? tokens.foreground : tokens.muted);
        const Rect box{r.x + 2.0f, r.y + 2.0f, r.width - 4.0f,
                       r.height - 4.0f};
        switch (i) {
            case 0:   // snap toggle
                DrawMagnetIcon(renderer, box, glyph);
                break;
            case 1:   // razor tool
                DrawRazorIcon(renderer, box, glyph);
                break;
            case 2:   // add marker at playhead
                DrawDiamond(
                    renderer,
                    Point{box.x + box.width * 0.5f,
                          box.y + box.height * 0.5f},
                    4.0f, glyph, 1.4f);
                break;
            case 3: {
                // fx summary badge — amber chip.
                const Rect chip{r.x, r.y + 4.0f, r.width, 12.0f};
                renderer.FillRect(
                    chip,
                    WithAlpha(tokens.warning_chip,
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

void TimelineSurface::RenderWaveform(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Clip& clip,
    const Rect& rect) {
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
    // Bright teal min/max strokes on the dark-teal clip body.
    const Color color = WithAlpha(tokens.waveform, 0.85f);
    renderer.DrawLine({rect.x, mid}, {rect.x + rect.width, mid},
                      WithAlpha(tokens.waveform, 0.25f), 1.0f);
    for (int px = 0; px < columns; ++px) {
        const double t0 = src0 + (src1 - src0) * px / columns;
        const double t1 = src0 + (src1 - src0) * (px + 1) / columns;
        int c0 = static_cast<int>(
            std::floor(t0 / wave->span_seconds * wave->columns));
        int c1 = static_cast<int>(
                     std::ceil(t1 / wave->span_seconds *
                               wave->columns)) -
                 1;
        c0 = std::clamp(c0, 0, wave->columns - 1);
        c1 = std::clamp(c1, c0, wave->columns - 1);
        float mn = 1.0f, mx = -1.0f;
        for (int c = c0; c <= c1; ++c) {
            mn = std::min(
                mn, wave->minmax[static_cast<std::size_t>(c) * 2u]);
            mx = std::max(
                mx, wave->minmax[static_cast<std::size_t>(c) * 2u +
                                      1u]);
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

void TimelineSurface::RenderThumbnails(
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

void TimelineSurface::Layout(const XER::UI::Layout::Rect& bounds) {
    SetBounds(bounds);
    timeline_area_ = bounds;
}

bool TimelineSurface::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::KeyCode;
    using XER::UI::Widgets::MouseButton;
    if (!IsEnabled() || project_ == nullptr) return false;

    if (event.type == Event::Type::MouseWheel) {
        if (event.is_alt) {
            // Alt+wheel scrolls the track stack vertically — plain
            // wheel stays zoom, Shift+wheel horizontal scroll.
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
                        (track_height_ + kTrackGap),
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
            const double next = std::clamp(px_per_second_ * factor,
                                           kMinZoom, kMaxZoom);
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
                // Expand chevron — taller lane, surface-side only.
                if (expanded_tracks_.count(hit.track) != 0u) {
                    expanded_tracks_.erase(hit.track);
                } else {
                    expanded_tracks_.insert(hit.track);
                }
            } else if (hit.button == 4) {
                SetStatus("Solo isn't wired into the track model yet");
            } else if (hit.button == 5) {
                SetStatus(
                    "Record arm isn't wired into the track model yet");
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

        // Junction-handle grab: the transition overlay's edge bars
        // resize the centered duration (live preview, commit on
        // mouse-up). Selection tool only — every other tool owns its
        // own edge semantics at the same pixels (razor cut, roll).
        if (active_tool_ == ToolId::Selection) {
            if (const TransitionHandleHit th = HitTransitionHandle(p);
                th.index >= 0) {
                const Transition& tr = project_->transitions[
                    static_cast<std::size_t>(th.index)];
                const Clip* from = FindClip(*project_, tr.from_clip);
                const bool locked =
                    from != nullptr && from->track >= 0 &&
                    from->track < static_cast<int>(
                        project_->tracks.size()) &&
                    project_->tracks[static_cast<std::size_t>(
                        from->track)].locked;
                if (locked) {
                    SetStatus("track is locked", true);
                } else {
                    transition_drag_ = true;
                    transition_drag_from_ = tr.from_clip;
                    transition_drag_to_ = tr.to_clip;
                    transition_drag_edge_ = th.edge;
                    transition_drag_orig_ = tr.duration;
                    transition_drag_preview_ = tr.duration;
                    transition_drag_max_ = TransitionDragLimit(tr);
                    SetStatus("Drag transition handle — " +
                              FormatTime(tr.duration));
                }
                Invalidate();
                return true;
            }
        }

        // Pointer events inside the track area belong to the active
        // tool — it resolves hit/drag/commit via ToolCommit. The
        // gesture bracket wraps the whole down dispatch so a tool
        // that commits on press (razor split) lands inside the same
        // macro scope as a pointer-up burst.
        const ToolContext ctx = MakeContext(event);
        TimelineTool* tool = ActiveToolImpl();
        bool used = false;
        if (tool != nullptr) {
            GestureScope(*this, "edit");
            used = tool->OnPointerDown(ctx, *this);
        }
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
            const int prev_th = hover_transition_;
            const TransitionHandleHit thover =
                transition_drag_ ? TransitionHandleHit{}
                                 : HitTransitionHandle(p);
            const bool thover_changed =
                thover.index != hover_transition_ ||
                (thover.index >= 0 &&
                 thover.edge != hover_transition_edge_);
            if (hover_id != hover_clip_id_ ||
                hover_header.track != hover_header_.track ||
                hover_header.button != hover_header_.button ||
                hover_tool != hover_toolbar_ || thover_changed) {
                hover_clip_id_ = hover_id;
                hover_header_ = hover_header;
                hover_toolbar_ = hover_tool;
                hover_transition_ = thover.index;
                hover_transition_edge_ = thover.edge;
                Invalidate();
            }
            // Resize-cursor affordance on entering a junction edge —
            // the framework has no OS cursor switch, so the widened
            // hot bar plus a status hint stand in for it.
            if (prev_th < 0 && thover.index >= 0 &&
                !transition_drag_) {
                SetStatus("Drag transition edge to resize duration");
            }
        }
        if (ruler_scrub_) {
            if (hooks_.on_seek) hooks_.on_seek(TimeAtX(p.x));
            return true;
        }
        if (transition_drag_) {
            // Centered-duration model: the dragged edge tracks the
            // pointer, the far edge mirrors it around the cut. The
            // handle ceiling was clamped at grab from the endpoint
            // clips' available source handles (TransitionDragLimit).
            const Transition* tr = nullptr;
            for (const Transition& t : project_->transitions) {
                if (t.from_clip == transition_drag_from_ &&
                    t.to_clip == transition_drag_to_) {
                    tr = &t;
                    break;
                }
            }
            const Clip* from =
                tr != nullptr ? FindClip(*project_, tr->from_clip)
                              : nullptr;
            if (tr == nullptr || from == nullptr) {
                transition_drag_ = false;
                Invalidate();
                return true;
            }
            const double cut = ClipEndSeconds(*from);
            const double t =
                SnapCandidate(TimeAtX(p.x), from->track);
            const double half =
                transition_drag_edge_ == TrimEdge::In ? cut - t
                                                      : t - cut;
            const double frame =
                project_->fps_num > 0
                    ? static_cast<double>(project_->fps_den) /
                          project_->fps_num
                    : 1.0 / 30.0;
            const double floor_d = std::max(0.02, frame);
            transition_drag_preview_ =
                std::clamp(2.0 * half, floor_d,
                           std::max(floor_d, transition_drag_max_));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Transition %.2f s",
                          transition_drag_preview_);
            SetStatus(buf);
            Invalidate();
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
        if (TimelineTool* tool = ActiveToolImpl();
            tool != nullptr && tool->IsDragging()) {
            const ToolContext ctx = MakeContext(event);
            return tool->OnPointerMove(ctx, *this);
        }
        return false;
    }

    if (event.type == Event::Type::MouseUp &&
        event.mouse_button == MouseButton::Left) {
        // Any release ends whatever drag was snapping — hide the
        // guide line before the drag branches below run.
        if (snap_indicator_t_ >= 0.0) {
            snap_indicator_t_ = -1.0;
            Invalidate();
        }
        if (ruler_scrub_) {
            ruler_scrub_ = false;
            return true;
        }
        if (transition_drag_) {
            transition_drag_ = false;
            const double committed = transition_drag_preview_;
            const double original = transition_drag_orig_;
            const std::string from =
                std::move(transition_drag_from_);
            const std::string to = std::move(transition_drag_to_);
            transition_drag_from_.clear();
            transition_drag_to_.clear();
            if (std::fabs(committed - original) > 1e-9 &&
                hooks_.on_set_transition_duration) {
                GestureScope(*this, "transition");
                hooks_.on_set_transition_duration(from, to, committed);
            }
            Invalidate();
            return true;
        }
        if (work_drag_ != WorkDrag::None) {
            work_drag_ = WorkDrag::None;
            const double in =
                std::min(work_drag_in_, work_drag_out_);
            const double out =
                std::max(work_drag_in_, work_drag_out_);
            if (hooks_.on_set_work_area) {
                GestureScope(*this, "work area");
                hooks_.on_set_work_area(in, out);
            }
            Invalidate();
            return true;
        }
        if (TimelineTool* tool = ActiveToolImpl();
            tool != nullptr && tool->IsDragging()) {
            const ToolContext ctx = MakeContext(event);
            GestureScope(*this, "edit");
            return tool->OnPointerUp(ctx, *this);
        }
        return false;
    }

    if (event.type == Event::Type::KeyDown) {
        if (event.key == KeyCode::Escape) {
            if (transition_drag_) {
                transition_drag_ = false;
                Invalidate();
                return true;
            }
            if (work_drag_ != WorkDrag::None) {
                work_drag_ = WorkDrag::None;
                Invalidate();
                return true;
            }
            if (TimelineTool* tool = ActiveToolImpl();
                tool != nullptr && tool->IsDragging()) {
                tool->Cancel(*this);
                return true;
            }
            return false;
        }
        const double frame_step = project_->fps_num > 0
            ? static_cast<double>(project_->fps_den) /
                  project_->fps_num
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
            GestureScope(*this, "delete");
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

void TimelineSurface::SelectClip(const std::string& clip_id) {
    if (hooks_.on_select_clip) hooks_.on_select_clip(clip_id);
}

double TimelineSurface::SnapCandidate(double seconds,
                                      int track) const {
    if (!snap_enabled_ || project_ == nullptr) {
        snap_indicator_t_ = -1.0;
        return seconds;
    }
    // CollectSnapPoints already feeds clip edges on the track (all
    // tracks when < 0), marker times, work-area edges and the
    // playhead.
    const double snapped =
        SnapTime(*project_, track, seconds, 8.0 / px_per_second_,
                 playhead_);
    // Snap indicator: a resolved-different result marks the guide
    // line's time; an unsnapped call hides it (the drag moved off
    // the target). MouseUp clears — see the release path.
    snap_indicator_t_ =
        std::fabs(snapped - seconds) > 1e-9 ? snapped : -1.0;
    return snapped;
}

void TimelineSurface::SetClipPreview(const std::string& clip_id,
                                     double start_seconds,
                                     double end_seconds) {
    clip_previews_[clip_id] = {start_seconds, end_seconds};
}

void TimelineSurface::ClearClipPreviews() { clip_previews_.clear(); }

void TimelineSurface::SetMarquee(bool active, float x0, float x1) {
    marquee_active_ = active;
    marquee_x0_ = x0;
    marquee_x1_ = x1;
}

void TimelineSurface::MoveClip(const std::string& clip_id,
                               double new_start) {
    if (hooks_.on_move_clip) hooks_.on_move_clip(clip_id, new_start);
}

void TimelineSurface::TrimClip(const std::string& clip_id,
                               TrimEdge edge, double new_point) {
    if (hooks_.on_trim_clip) {
        hooks_.on_trim_clip(clip_id, edge, new_point);
    }
}

void TimelineSurface::RippleTrim(const std::string& clip_id,
                                 TrimEdge edge, double new_point) {
    if (hooks_.on_ripple_trim) {
        hooks_.on_ripple_trim(clip_id, edge, new_point);
    } else {
        TrimClip(clip_id, edge, new_point);
    }
}

void TimelineSurface::SplitClip(const std::string& clip_id,
                                double at_seconds) {
    if (hooks_.on_split_clip) {
        hooks_.on_split_clip(clip_id, at_seconds);
    }
}

void TimelineSurface::RollingEdit(const std::string& from_id,
                                  const std::string& to_id,
                                  double delta_seconds) {
    if (hooks_.on_rolling_edit) {
        hooks_.on_rolling_edit(from_id, to_id, delta_seconds);
    }
}

void TimelineSurface::SlipClip(const std::string& clip_id,
                               double delta_seconds) {
    if (hooks_.on_slip_clip) {
        hooks_.on_slip_clip(clip_id, delta_seconds);
    }
}

void TimelineSurface::SlideClip(const std::string& clip_id,
                                double delta_seconds) {
    if (hooks_.on_slide_clip) {
        hooks_.on_slide_clip(clip_id, delta_seconds);
    }
}

void TimelineSurface::SetClipRate(const std::string& clip_id,
                                  double new_rate) {
    if (hooks_.on_rate_stretch) {
        hooks_.on_rate_stretch(clip_id, new_rate);
    }
}

void TimelineSurface::PanBy(double seconds) {
    scroll_seconds_ = std::max(0.0, scroll_seconds_ + seconds);
    Invalidate();
}

void TimelineSurface::ZoomAt(double factor, double anchor_seconds) {
    const double next =
        std::clamp(px_per_second_ * factor, kMinZoom, kMaxZoom);
    if (next == px_per_second_) return;
    const float anchor_x = XAtTime(anchor_seconds);
    px_per_second_ = next;
    scroll_seconds_ = std::max(
        0.0, anchor_seconds -
                 (anchor_x - (timeline_area_.x + kHeaderWidth)) /
                     next);
    Invalidate();
}

void TimelineSurface::ZoomToRange(double t0, double t1) {
    if (!(t1 > t0)) return;
    const double width_px =
        std::max(1.0f, timeline_area_.width - kHeaderWidth);
    px_per_second_ =
        std::clamp(width_px / (t1 - t0), kMinZoom, kMaxZoom);
    scroll_seconds_ = std::max(0.0, t0);
    Invalidate();
}

void TimelineSurface::Tick(double dt_seconds) { (void)dt_seconds; }

// RAII gesture bracket — fires the optional
// TimelineHooks::on_gesture_begin/on_gesture_end pair around a
// burst of edit hooks. The bridge maps the pair onto
// TimelineHistory::BeginMacro/EndMacro so one pointer gesture is
// one undo step (the contract's documented requirement). Unset
// hooks -> no-op, matching the legacy every-call-its-own-entry
// behavior.
TimelineSurface::GestureScope::GestureScope(TimelineSurface& surface,
                                            const char* label) {
    if (surface.hooks_.on_gesture_begin) {
        surface.hooks_.on_gesture_begin(label);
        active_ = true;
        surface_ = &surface;
    }
}
TimelineSurface::GestureScope::~GestureScope() {
    if (active_ && surface_->hooks_.on_gesture_end) {
        surface_->hooks_.on_gesture_end();
    }
}

bool TimelineSurface::ShowContextMenu(
    const XER::UI::Layout::Point p) {
    namespace Widgets = XER::UI::Widgets;
    if (project_ == nullptr || !timeline_area_.Contains(p)) {
        return false;
    }

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
        if (const Marker* marker = MarkerAtX(p.x);
            marker != nullptr) {
            Widgets::ContextMenu::Item clear;
            clear.label = "Clear Marker";
            clear.danger = true;
            clear.action = [this, tm = marker->time] {
                if (hooks_.on_remove_marker) {
                    hooks_.on_remove_marker(tm);
                }
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
            if (hooks_.on_set_work_area) {
                hooks_.on_set_work_area(0.0, 0.0);
            }
        };
        items.push_back(std::move(add));
        items.push_back(std::move(win));
        items.push_back(std::move(wout));
        items.push_back(std::move(reset));
    } else {
        // Track lanes: clip menu or an "add marker" fallback on
        // empty space.
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
                        {"Add Color Matte Here",
                         SourceKind::ColorMatte},
                        {"Add Text Here", SourceKind::Text},
                        {"Add Color Bars Here",
                         SourceKind::ColorBars},
                    };
                    bool first = true;
                    for (const auto& ins : kInserts) {
                        Widgets::ContextMenu::Item item;
                        item.label = ins.label;
                        item.separator_before = first;
                        item.action =
                            [this, lane, kind = ins.kind, t] {
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
            split.enabled =
                t > clip.timeline_start &&
                t < clip.timeline_start + clip.duration;
            split.action = [this, id, t] {
                if (hooks_.on_split_clip) {
                    hooks_.on_split_clip(id, t);
                }
            };
            items.push_back(std::move(split));

            // Junction pick: right-click on a clip's out-edge offers
            // every supported transition type to the next clip (the
            // shell resolves the adjacent clip and supplies the
            // default duration for a <= 0 request). JS
            // script-transitions are a shell-side list (bridge seam)
            // — the hook already carries Type::Script + script_id.
            if (hit.part == ClipPart::EdgeOut) {
                bool first = true;
                for (const Transition::Type type :
                     AllTransitionTypes()) {
                    if (type == Transition::Type::Script ||
                        type == Transition::Type::Xfade ||
                        type == Transition::Type::XerFx) {
                        continue;   // JS + xfade + xer.tr list below
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
                // XER-native hand-written transitions
                // (transitions_lib.h) — always available, listed
                // ahead of the FFmpeg set. Same flat-menu header-row
                // convention as xfade below; the variant string is
                // the "xer.tr.*" id.
                {
                    const char* last_group = nullptr;
                    for (const XerTransitionInfo& xt :
                         XerTransitions()) {
                        if (last_group == nullptr ||
                            std::strcmp(last_group, xt.group) != 0) {
                            Widgets::ContextMenu::Item header;
                            header.label =
                                std::string("── XER ") + xt.group +
                                " ──";
                            header.enabled = false;
                            header.separator_before = true;
                            items.push_back(std::move(header));
                            last_group = xt.group;
                        }
                        Widgets::ContextMenu::Item trans;
                        trans.label = std::string("XER: ") + xt.label;
                        trans.action =
                            [this, id, tid = xt.id] {
                                if (hooks_.on_add_transition) {
                                    hooks_.on_add_transition(
                                        id, Transition::Type::XerFx,
                                        -1.0, tid);
                                }
                            };
                        items.push_back(std::move(trans));
                    }
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

            // Smart Reframe / track-object / AI / transcript items —
            // each gates on its hook being wired, so the menu grows
            // as the shell lands those seams (M3+ panels).
            if (hooks_.on_smart_reframe != nullptr) {
                const bool scannable =
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment;
                for (const auto& [label, ratio] :
                     {std::pair{"9:16", 0.5625},
                      std::pair{"1:1", 1.0},
                      std::pair{"4:5", 0.8}}) {
                    Widgets::ContextMenu::Item sr;
                    sr.label = "Smart Reframe \xe2\x86\x92 " +
                               std::string(label);
                    sr.enabled = scannable;
                    sr.action = [this, id, ratio] {
                        if (hooks_.on_smart_reframe) {
                            hooks_.on_smart_reframe(id, ratio);
                        }
                    };
                    items.push_back(std::move(sr));
                }
            }

            if (hooks_.on_track_object != nullptr ||
                hooks_.on_pin_title != nullptr ||
                hooks_.on_pin_mosaic != nullptr) {
                const bool scannable =
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment;
                Widgets::ContextMenu::Item track;
                track.label = "Track object\xe2\x80\xa6";
                track.separator_before = true;
                track.enabled = scannable &&
                                hooks_.on_track_object != nullptr;
                track.action = [this, id] {
                    if (hooks_.on_track_object) {
                        hooks_.on_track_object(id, "");
                    }
                };
                items.push_back(std::move(track));
                if (hooks_.on_track_object != nullptr) {
                    constexpr std::size_t kMaxClassItems = 8;
                    for (std::size_t i = 0;
                         i < clip.object_track.classes.size() &&
                         i < kMaxClassItems; ++i) {
                        const std::string label =
                            clip.object_track.classes[i];
                        Widgets::ContextMenu::Item narrowed;
                        narrowed.label =
                            "Track \xe2\x80\x98" + label +
                            "\xe2\x80\x99\xe2\x80\xa6";
                        narrowed.enabled = scannable;
                        narrowed.checked =
                            label == clip.object_track.label;
                        narrowed.action = [this, id, label] {
                            if (hooks_.on_track_object) {
                                hooks_.on_track_object(id, label);
                            }
                        };
                        items.push_back(std::move(narrowed));
                    }
                }
                const bool has_track =
                    !clip.object_track.samples.empty();
                Widgets::ContextMenu::Item pin_title;
                pin_title.label = "Pin \xe2\x86\x92 Title";
                pin_title.enabled = has_track &&
                                    hooks_.on_pin_title != nullptr;
                pin_title.action = [this, id] {
                    if (hooks_.on_pin_title) {
                        hooks_.on_pin_title(id);
                    }
                };
                items.push_back(std::move(pin_title));
                Widgets::ContextMenu::Item pin_mosaic;
                pin_mosaic.label = "Pin \xe2\x86\x92 Mosaic";
                pin_mosaic.enabled = has_track &&
                                     hooks_.on_pin_mosaic != nullptr;
                pin_mosaic.action = [this, id] {
                    if (hooks_.on_pin_mosaic) {
                        hooks_.on_pin_mosaic(id);
                    }
                };
                items.push_back(std::move(pin_mosaic));
                if (has_track || !clip.object_track.label.empty()) {
                    Widgets::ContextMenu::Item clear;
                    clear.label = "Clear Object Track";
                    clear.enabled =
                        hooks_.on_clear_track != nullptr;
                    clear.action = [this, id] {
                        if (hooks_.on_clear_track) {
                            hooks_.on_clear_track(id);
                        }
                    };
                    items.push_back(std::move(clear));
                }
            }

            // Auto-blur faces: YuNet scan -> face ClipTrack ->
            // xer.fx.face_blur. Its own gate (like the track block)
            // so the item shows whenever the shell wires the seam.
            if (hooks_.on_face_blur != nullptr) {
                const bool scannable =
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment;
                Widgets::ContextMenu::Item fb;
                fb.label = "Auto-blur Faces";
                fb.separator_before = true;
                fb.enabled = scannable;
                fb.action = [this, id] {
                    if (hooks_.on_face_blur) {
                        hooks_.on_face_blur(id);
                    }
                };
                items.push_back(std::move(fb));
            }

            // Track Mask (AI): the shell arms the monitor click seam
            // — the next frame click is the SAM2 prompt — then an
            // async per-frame re-prompt scan lands Clip::mask_track
            // + xer.fx.track_mask (docs/wiki/research/
            // 2026-11-23-hf-org-scan.md §1). Same gate shape as
            // face-blur.
            if (hooks_.on_track_mask != nullptr ||
                hooks_.on_clear_mask != nullptr) {
                const bool scannable =
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment;
                Widgets::ContextMenu::Item tm;
                tm.label = "Track Mask (AI)\xe2\x80\xa6";
                tm.enabled = scannable &&
                             hooks_.on_track_mask != nullptr;
                tm.action = [this, id] {
                    if (hooks_.on_track_mask) {
                        hooks_.on_track_mask(id);
                    }
                };
                items.push_back(std::move(tm));
                if (!clip.mask_track.samples.empty()) {
                    Widgets::ContextMenu::Item cm;
                    cm.label = "Clear Mask Track";
                    cm.enabled =
                        hooks_.on_clear_mask != nullptr;
                    cm.action = [this, id] {
                        if (hooks_.on_clear_mask) {
                            hooks_.on_clear_mask(id);
                        }
                    };
                    items.push_back(std::move(cm));
                }
            }

            // Smooth slow-mo (AI): RIFE bounce of the consumed span,
            // then the clip repoints at the baked file. Enabled only
            // for media clips already slowed (playback_rate < 1) —
            // the bake semantics are "make THIS slow-mo smooth", not
            // a speed-change tool (that's the rate stretcher's job).
            if (hooks_.on_ai_slowmo != nullptr) {
                const bool slowed_media =
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment &&
                    clip.playback_rate < 1.0 - 1e-3;
                Widgets::ContextMenu::Item sm;
                sm.label = "Smooth slow-mo (AI)";
                // No separator — it sits inside the face-blur
                // item's AI group.
                sm.enabled = slowed_media;
                sm.action = [this, id] {
                    if (hooks_.on_ai_slowmo) {
                        hooks_.on_ai_slowmo(id);
                    }
                };
                items.push_back(std::move(sm));
            }

            if (hooks_.on_ai_find_objects != nullptr ||
                hooks_.on_ai_clear_tags != nullptr) {
                Widgets::ContextMenu::Item find;
                find.label = "AI: Find Objects";
                find.separator_before = true;
                find.enabled =
                    hooks_.on_ai_find_objects != nullptr &&
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment;
                find.action = [this, id] {
                    if (hooks_.on_ai_find_objects) {
                        hooks_.on_ai_find_objects(id);
                    }
                };
                items.push_back(std::move(find));
                if (!clip.tags.empty()) {
                    Widgets::ContextMenu::Item clear;
                    clear.label = "AI: Clear Tags";
                    clear.enabled =
                        hooks_.on_ai_clear_tags != nullptr;
                    clear.action = [this, id] {
                        if (hooks_.on_ai_clear_tags) {
                            hooks_.on_ai_clear_tags(id);
                        }
                    };
                    items.push_back(std::move(clear));
                }
            }

            // Text-based editing entry: the Transcript dock binds to
            // this clip and (on a cache miss) starts whisper.
            {
                Widgets::ContextMenu::Item et;
                et.label = "Edit by Transcript";
                et.separator_before = true;
                et.enabled =
                    hooks_.on_edit_transcript != nullptr &&
                    clip.source.kind == SourceKind::Media &&
                    !clip.source_path.empty() && !clip.is_adjustment;
                et.action = [this, id] {
                    if (hooks_.on_edit_transcript) {
                        hooks_.on_edit_transcript(id);
                    }
                };
                items.push_back(std::move(et));
            }

            Widgets::ContextMenu::Item del;
            del.label = "Delete";
            del.shortcut = "Del";
            del.danger = true;
            del.separator_before = true;
            del.action = [this, id] {
                if (hooks_.on_delete_clip) {
                    hooks_.on_delete_clip(id);
                }
            };
            Widgets::ContextMenu::Item ripple;
            ripple.label = "Ripple Delete";
            ripple.danger = true;
            ripple.action = [this, id] {
                if (hooks_.on_ripple_delete) {
                    hooks_.on_ripple_delete(id);
                }
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

}  // namespace XER::VideoEditor::Ui2
