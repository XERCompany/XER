#include "monitor_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "app_runtime/app_runtime.h"
#include "app_runtime/ui_manifest.h"
#include "color/ocio_transform.h"
#include "helper/path_utils.h"
#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "scopes_widget.h"
#include "theme/tokens.h"
#include "widgets/badge.h"
#include "widgets/button.h"
#include "widgets/context_menu.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Path = XER::Helper::Path;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

// assets/color.yaml wires the program monitor's OCIO display
// transform. The resolved path is immutable for the session, so the
// config is lazily loaded on the first composited frame — a missing
// yaml, a disabled/failed config, or an absent OpenColorIO runtime
// (ApplyDisplayTransform probes that itself) all degrade to
// pass-through.
const XER::VideoEditor::Color::OcioConfig& MonitorOcioConfig() {
    static bool loaded = false;
    static XER::VideoEditor::Color::OcioConfig cfg;
    if (!loaded) {
        loaded = true;
        const std::string& yaml =
            AppRuntime::SharedAssetsConfig().color_yaml;
        std::string error;
        if (yaml.empty() ||
            !XER::VideoEditor::Color::LoadOcioConfig(
                Path::FromUtf8(yaml), &cfg, &error)) {
            cfg.enabled = false;
        }
    }
    return cfg;
}

// In-place OCIO display transform on a packed BGRA8 monitor frame
// (stride == width*4, alpha preserved). No-op when color.yaml is
// absent/disabled or the buffer can't hold width*height pixels.
void ApplyMonitorDisplayTransform(std::vector<std::uint8_t>* bgra,
                                  int width, int height) {
    const XER::VideoEditor::Color::OcioConfig& cfg = MonitorOcioConfig();
    if (!cfg.enabled || bgra == nullptr || width <= 0 || height <= 0) {
        return;
    }
    if (bgra->size() < static_cast<std::size_t>(width) *
                           static_cast<std::size_t>(height) * 4u) {
        return;
    }
    XER::VideoEditor::Color::ApplyDisplayTransform(
        bgra->data(), width, height, width * 4, cfg);
}

// ---- audio meter ballistics (Premiere-style) ----
constexpr float kMeterMinDb = -60.0f;      // bottom of the meter scale
constexpr float kMeterFallDbPerSec = 20.0f;   // bar falloff
constexpr double kPeakHoldSeconds = 1.6;      // peak mark hold time
constexpr float kPeakFallDbPerSec = 12.0f;    // peak mark decay after hold
constexpr float kMeterYellowDb = -12.0f;   // green -> yellow zone edge
constexpr float kMeterRedDb = -3.0f;       // yellow -> red zone edge

float ToDb(float linear_peak) {
    if (linear_peak <= 0.0f || !std::isfinite(linear_peak)) {
        return kMeterMinDb;
    }
    return std::max(kMeterMinDb, 20.0f * std::log10(linear_peak));
}

// Fixed meter palette — zone colors are an industry convention, not a
// theme concern.
Color MeterGreen() { return {0.22f, 0.78f, 0.35f, 1.0f}; }
Color MeterYellow() { return {0.94f, 0.78f, 0.18f, 1.0f}; }
Color MeterRed() { return {0.93f, 0.26f, 0.22f, 1.0f}; }

Color WithAlpha(Color c, float alpha) {
    c.a = alpha;
    return c;
}

Color MarkerColor(const Marker& marker) {
    return {marker.color[0] / 255.0f, marker.color[1] / 255.0f,
            marker.color[2] / 255.0f, marker.color[3] / 255.0f};
}

// ---- scopes overlay ----
// monitor_widget.h is untouched by this change, so the scopes widget
// and its visibility flag live in a file-local table keyed on the
// MonitorWidget instance (erased in ~MonitorWidget).
struct MonitorScopes {
    ScopesWidget widget;
    bool visible = false;
};

std::unordered_map<MonitorWidget*, MonitorScopes>& ScopesTable() {
    static std::unordered_map<MonitorWidget*, MonitorScopes> table;
    return table;
}

MonitorScopes& ScopesState(MonitorWidget* self) {
    return ScopesTable()[self];
}

// Width of the 8th transport button appended after Focus (see
// ScopesButtonRect); also counted in OverlayButtonRect's centering.
constexpr float kScopesButtonWidth = 52.0f;

// Geometry mirrors OverlayButtonRect(): 7 buttons
// {34,30,30,52,52,52,30} + 6 px gaps, then the scopes toggle last.
Layout::Rect ScopesButtonRect(const Layout::Rect& row) {
    const float gap = 6.0f;
    const float widths[] = {34.0f, 30.0f, 30.0f, 52.0f, 52.0f, 52.0f,
                            30.0f};
    float strip = gap * 7.0f + kScopesButtonWidth;
    for (float w : widths) strip += w;
    float x = row.x;
    if (AppRuntime::SharedLayoutConfig().transport_center) {
        x += std::max(0.0f, (row.width - strip) * 0.5f);
    }
    return {x + strip - kScopesButtonWidth, row.y, kScopesButtonWidth,
            row.height};
}

// Top-right scopes overlay inside the video area, kept clear of the
// audio meters hugging the right edge.
Layout::Rect ScopesOverlayRect(const Layout::Rect& video_area) {
    const float w =
        std::max(0.0f, std::min(256.0f, video_area.width - 40.0f));
    const float h =
        std::max(0.0f, std::min(190.0f, video_area.height - 16.0f));
    return {video_area.x + video_area.width - w - 24.0f,
            video_area.y + 8.0f, w, h};
}

std::string FormatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "00:00:00.000";
    const int hours = static_cast<int>(seconds / 3600.0);
    seconds -= hours * 3600.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    seconds -= minutes * 60.0;
    const int whole = static_cast<int>(seconds);
    const int millis = static_cast<int>(
        std::round((seconds - whole) * 1000.0));
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", hours,
                  minutes, whole, millis == 1000 ? 999 : millis);
    return buffer;
}

// Big monitor timecode HH:MM:SS:FF — FF is the frame index inside the
// current second at the project's nominal (integer-rounded) rate.
std::string FormatTimecode(double seconds, int fps_num, int fps_den) {
    if (!std::isfinite(seconds) || seconds < 0.0) seconds = 0.0;
    const int nominal = fps_num > 0 && fps_den > 0
                            ? std::max(1, static_cast<int>(std::lround(
                                              static_cast<double>(fps_num) /
                                              fps_den)))
                            : 30;
    const int hours = static_cast<int>(seconds / 3600.0);
    seconds -= hours * 3600.0;
    const int minutes = static_cast<int>(seconds / 60.0);
    seconds -= minutes * 60.0;
    const int whole = static_cast<int>(seconds);
    int frame = static_cast<int>((seconds - whole) * nominal);
    frame = std::clamp(frame, 0, nominal - 1);
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d:%02d", hours,
                  minutes, whole, frame);
    return buffer;
}

}  // namespace

MonitorWidget::MonitorWidget(Hooks hooks) : hooks_(std::move(hooks)) {
    SetFocusable(true);
}

MonitorWidget::~MonitorWidget() {
    ScopesTable().erase(this);
    compositor_.Cancel();
    compositor_.Shutdown();
}

void MonitorWidget::EnsureChildren() {
    if (transport_children_) return;
    transport_children_ = true;
    namespace Widgets = XER::UI::Widgets;
    // Icon-capable buttons resolve through the pack (idempotent).
    VeIcons::InstallButtonIconResolver();

    const auto make = [this](const char* icon, const char* tooltip) {
        auto button = std::make_shared<Widgets::Button>(
            "", Widgets::ButtonStyle::Secondary);
        // Transport keys must keep routing to the monitor / global
        // shortcuts — the row's buttons take pointer input only.
        button->SetFocusable(false);
        if (icon != nullptr) button->SetIcon(icon);
        button->SetTooltip(tooltip);
        AddChild(button);
        return button;
    };

    play_btn_ = make("transport.play", "Play / pause (Space)");
    play_btn_->SetOnClick([this] {
        if (on_play_toggle_) {
            on_play_toggle_();
        } else {
            SetPlaying(!playing_);
        }
    });
    const auto step = [this](int dir) {
        if (on_frame_step_) {
            on_frame_step_(dir);
            return;
        }
        const double frame_dt =
            project_.fps_num > 0
                ? static_cast<double>(project_.fps_den) /
                      project_.fps_num
                : 1.0 / 30.0;
        SetPlaying(false);
        SetTime(std::clamp(time_ + dir * frame_dt, 0.0,
                           project_.duration()),
                true);
        EmitTime(time_);
    };
    prev_btn_ = make("transport.prev", "Previous frame");
    prev_btn_->SetOnClick([step] { step(-1); });
    next_btn_ = make("transport.next", "Next frame");
    next_btn_->SetOnClick([step] { step(1); });
    safe_btn_ = make("transport.safe", "Safe margins");
    safe_btn_->SetOnClick([this] { SetSafeMargins(!safe_margins_); });
    grid_btn_ = make("transport.grid", "Rule-of-thirds grid");
    grid_btn_->SetOnClick([this] { SetGrid(!grid_); });
    loop_btn_ = make("transport.loop", "Loop playback");
    loop_btn_->SetOnClick([this] { SetLoop(!loop_); });
    focus_btn_ = make("transport.fullscreen", "Monitor focus");
    focus_btn_->SetOnClick([this] {
        if (hooks_.on_toggle_focus) hooks_.on_toggle_focus();
    });
    // No scopes icon ships in the pack — text chip like before.
    scopes_btn_ = std::make_shared<Widgets::Button>(
        "SCOPES", Widgets::ButtonStyle::Secondary);
    scopes_btn_->SetFocusable(false);
    scopes_btn_->SetTooltip("Toggle scopes overlay");
    scopes_btn_->SetOnClick([this] {
        MonitorScopes& s = ScopesState(this);
        s.visible = !s.visible;
        Invalidate();
    });
    AddChild(scopes_btn_);

    quality_badge_ = std::make_shared<Widgets::Badge>(
        quality_tag_, Widgets::BadgeVariant::Secondary);
    quality_badge_->SetTooltip(
        "Preview playback quality (right-click to change)");
    quality_badge_->SetVisible(!quality_tag_.empty());
    AddChild(quality_badge_);
}

void MonitorWidget::SyncTransportChildren() {
    if (!transport_children_) return;
    namespace Widgets = XER::UI::Widgets;
    const auto style = [](bool active) {
        return active ? Widgets::ButtonStyle::Primary
                      : Widgets::ButtonStyle::Secondary;
    };
    play_btn_->SetIcon(TransportPlaying() ? "transport.pause"
                                          : "transport.play");
    play_btn_->SetStyle(style(TransportPlaying()));
    safe_btn_->SetStyle(style(safe_margins_));
    grid_btn_->SetStyle(style(grid_));
    loop_btn_->SetStyle(style(loop_));
    scopes_btn_->SetStyle(style(ScopesState(this).visible));
}

void MonitorWidget::SetProject(const Project& project) {
    project_ = project;
    // Project edits invalidate any marker/work-area derived state only
    // visually — the compositor snapshot is rebuilt as before.
    RebuildCompositor();
    SetTime(std::min(time_, project_.duration()), true);
}

void MonitorWidget::RebuildCompositor() {
    compositor_.Shutdown();
    compositor_ready_ = false;
    frame_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    rendered_time_ = -1.0;
    frame_dirty_ = true;
    // Frame buffer is gone — detach the scopes' source pointer.
    ScopesState(this).widget.SetFrame(nullptr, 0, 0, 0);

    std::string error;
    std::vector<std::string> warnings;
    if (project_.duration() <= 0.0 || project_.width <= 0 ||
        project_.height <= 0 || project_.fps_num <= 0 ||
        project_.fps_den <= 0) {
        status_text_ = "Empty timeline";
        status_error_ = false;
        Invalidate();
        return;
    }
    if (!compositor_.Init(project_, CompositorOptions{}, &error,
                          &warnings)) {
        status_text_ = error.empty() ? "Compositor init failed" : error;
        status_error_ = true;
        SetStatus(status_text_, true);
        Invalidate();
        return;
    }
    compositor_ready_ = true;
    if (!warnings.empty()) {
        SetStatus(warnings.front(), true);
    } else {
        SetStatus("Ready", false);
    }
}

void MonitorWidget::SetTime(double seconds, bool force_render) {
    const double duration = std::max(0.0, project_.duration());
    const double t = std::clamp(seconds, 0.0, duration);
    if (external_playback_) {
        // An engine drives frames; only the readout/ruler need the time.
        time_ = t;
        Invalidate();
        return;
    }
    if (t < time_ - 0.0005 && compositor_ready_) {
        // Compositor sessions are optimized for forward playback. A
        // backward scrub rebinds the snapshot so decoders seek correctly.
        RebuildCompositor();
    }
    time_ = t;
    frame_dirty_ = frame_dirty_ || force_render ||
                   std::abs(t - rendered_time_) > 0.0005;
    if (force_render) {
        std::string ignored;
        RenderCurrentFrame(&ignored);
    }
    Invalidate();
}

void MonitorWidget::SetPlaying(bool playing) {
    if (playing && time_ >= project_.duration()) {
        SetTime(0.0, true);
    }
    playing_ = playing;
    frame_dirty_ = frame_dirty_ || playing;
    Invalidate();
}

void MonitorWidget::SetPeakLevels(float left, float right) {
    peak_in_l_ = std::isfinite(left) ? std::max(0.0f, left) : 0.0f;
    peak_in_r_ = std::isfinite(right) ? std::max(0.0f, right) : 0.0f;
    Invalidate();
}

void MonitorWidget::SetSafeMargins(bool on) {
    if (safe_margins_ == on) return;
    safe_margins_ = on;
    Invalidate();
}

void MonitorWidget::SetGrid(bool on) {
    if (grid_ == on) return;
    grid_ = on;
    Invalidate();
}

void MonitorWidget::SetLoop(bool on) {
    if (loop_ == on) return;
    loop_ = on;
    Invalidate();
}

void MonitorWidget::SetLoopRange(double in_seconds, double out_seconds) {
    loop_in_ = std::max(0.0, in_seconds);
    loop_out_ = std::max(0.0, out_seconds);
    Invalidate();
}

void MonitorWidget::SetQualityTag(const std::string& tag) {
    if (quality_tag_ == tag) return;
    quality_tag_ = tag;
    if (quality_badge_) {
        quality_badge_->SetText(tag);
        quality_badge_->SetVisible(!tag.empty());
    }
    Invalidate();
}

void MonitorWidget::SetExternalPlayback(bool on) {
    if (external_playback_ == on) return;
    external_playback_ = on;
    if (!on) {
        // Hand control back to the internal compositor: re-render a
        // still at the current time on the next Tick.
        frame_dirty_ = true;
    }
    Invalidate();
}

void MonitorWidget::SetExternalFrame(const std::vector<std::uint8_t>& bgra,
                                     int width, int height) {
    if (bgra.empty() || width <= 0 || height <= 0) return;
    // A malformed engine frame (buffer shorter than w*h*4) would make
    // the display transform and scope scan read out of bounds — drop
    // it instead. Division form avoids a w*h*4 overflow.
    if (bgra.size() / 4u <
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height)) {
        return;
    }
    frame_ = bgra;
    frame_width_ = width;
    frame_height_ = height;
    ApplyMonitorDisplayTransform(&frame_, width, height);
    MonitorScopes& scopes = ScopesState(this);
    if (scopes.visible) {
        scopes.widget.SetFrame(frame_.data(), frame_width_,
                               frame_height_, frame_width_ * 4);
    }
    Invalidate();
}

void MonitorWidget::SetOnPlayToggle(std::function<void()> cb) {
    on_play_toggle_ = std::move(cb);
    Invalidate();
}

void MonitorWidget::SetOnSeekRequest(std::function<void(double)> cb) {
    on_seek_request_ = std::move(cb);
}

void MonitorWidget::SetOnFrameStep(std::function<void(int)> cb) {
    on_frame_step_ = std::move(cb);
}

void MonitorWidget::SetOnQualityChange(
    std::function<void(float)> cb) {
    on_quality_ = std::move(cb);
}

void MonitorWidget::SetEnginePlaying(bool playing) {
    if (engine_playing_ == playing) return;
    engine_playing_ = playing;
    Invalidate();
}

void MonitorWidget::EmitTime(double t) {
    if (hooks_.on_time_changed) {
        hooks_.on_time_changed(t);
    }
}

bool MonitorWidget::RenderCurrentFrame(std::string* error_out) {
    if (!compositor_ready_) {
        if (error_out != nullptr) *error_out = status_text_;
        return false;
    }
    std::string error;
    if (!compositor_.RenderFrameAt(time_, &frame_, &error)) {
        status_text_ = error.empty() ? "Unable to render frame" : error;
        status_error_ = true;
        SetStatus(status_text_, true);
        if (error_out != nullptr) *error_out = status_text_;
        return false;
    }
    frame_width_ = compositor_.CanvasWidth();
    frame_height_ = compositor_.CanvasHeight();
    // Same display transform as externally pumped frames — paused
    // stills and playback stay color-managed identically.
    ApplyMonitorDisplayTransform(&frame_, frame_width_, frame_height_);
    MonitorScopes& scopes = ScopesState(this);
    if (scopes.visible) {
        scopes.widget.SetFrame(frame_.data(), frame_width_,
                               frame_height_, frame_width_ * 4);
    }
    rendered_time_ = time_;
    frame_dirty_ = false;
    status_text_ = "Ready";
    status_error_ = false;
    Invalidate();
    return true;
}

void MonitorWidget::SetStatus(const std::string& text, bool is_error) {
    status_text_ = text;
    status_error_ = is_error;
    if (hooks_.on_status) {
        hooks_.on_status(text, is_error);
    }
}

// ---- geometry ----

Layout::Rect MonitorWidget::VideoAreaRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const float chrome_bottom =
        16.0f + 2.0f +   // ruler strip + gap
        28.0f +          // transport row
        pad;
    return {bounds.x + pad, bounds.y + 34.0f,
            bounds.width - pad * 2.0f,
            std::max(60.0f, bounds.height - 34.0f - chrome_bottom)};
}

Layout::Rect MonitorWidget::RulerRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const auto video = VideoAreaRect();
    return {bounds.x + pad, video.y + video.height + 2.0f,
            bounds.width - pad * 2.0f, 14.0f};
}

Layout::Rect MonitorWidget::TransportRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const auto ruler = RulerRect();
    return {bounds.x + pad, ruler.y + ruler.height + 2.0f,
            bounds.width - pad * 2.0f, 26.0f};
}

Layout::Rect MonitorWidget::OverlayButtonRect(OverlayButton button) const {
    const auto row = TransportRect();
    const float gap = 6.0f;
    float x = row.x;
    const float widths[] = {34.0f, 30.0f, 30.0f, 52.0f, 52.0f,
                            52.0f, 30.0f};
    // assets/ui/layout.yaml — Monitor.transport_center centers the
    // transport strip instead of left-aligning it.
    if (AppRuntime::SharedLayoutConfig().transport_center) {
        float total = gap * 6.0f;
        for (float w : widths) total += w;
        // + gap + the scopes toggle appended after Focus (see
        // ScopesButtonRect) so the whole 8-button strip stays centered.
        total += gap + kScopesButtonWidth;
        x = row.x + std::max(0.0f, (row.width - total) * 0.5f);
    }
    const int index = static_cast<int>(button);
    for (int i = 0; i < index; ++i) x += widths[i] + gap;
    return {x, row.y, widths[index], row.height};
}

Layout::Rect MonitorWidget::FrameDestRect() const {
    const auto video_area = VideoAreaRect();
    if (frame_.empty() || frame_width_ <= 0 || frame_height_ <= 0) {
        return video_area;
    }
    const float source_aspect =
        static_cast<float>(frame_width_) / frame_height_;
    const float area_aspect = video_area.width / video_area.height;
    float w = video_area.width;
    float h = video_area.height;
    if (source_aspect > area_aspect) {
        h = w / source_aspect;
    } else {
        w = h * source_aspect;
    }
    return {video_area.x + (video_area.width - w) * 0.5f,
            video_area.y + (video_area.height - h) * 0.5f, w, h};
}

std::pair<double, double> MonitorWidget::EffectiveLoopRange() const {
    if (loop_out_ > loop_in_) {
        return {loop_in_, loop_out_};
    }
    if (project_.work_out > project_.work_in) {
        return {project_.work_in, project_.work_out};
    }
    return {0.0, 0.0};
}

const Marker* MonitorWidget::ActiveMarker() const {
    const Marker* active = nullptr;
    for (const Marker& marker : project_.markers) {
        if (marker.time <= time_ + 0.0005) {
            active = &marker;  // sorted by time — keep the latest match
        } else {
            break;
        }
    }
    return active;
}

double MonitorWidget::RulerTimeAtX(float x) const {
    const auto ruler = RulerRect();
    const double duration = project_.duration();
    if (ruler.width <= 0.0f || duration <= 0.0) return 0.0;
    const float u = std::clamp((x - ruler.x) / ruler.width, 0.0f, 1.0f);
    return u * duration;
}

// ---- render ----

void MonitorWidget::Render(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    SyncTransportChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);

    const float pad = tokens.spacing_medium;
    const Layout::Rect header{bounds.x + pad, bounds.y + pad * 0.6f,
                              bounds.width - pad * 2.0f, 24.0f};
    // assets/ui/strings.ini — program_title overrides the header.
    renderer.DrawText(
        AppRuntime::SharedUiStrings().Get("program_title", "PROGRAM"),
        header, {tokens.muted, tokens.font_small, 700});
    // The preview-quality chip ("Full"/"1/2"/"1/4"...) is a Badge
    // child top-right — it renders itself after this pass.

    const auto video_area = VideoAreaRect();
    renderer.FillRect(video_area, tokens.background, tokens.radius_medium);
    renderer.StrokeRect(video_area, tokens.border, 1.0f,
                        tokens.radius_medium);

    if (!frame_.empty() && frame_width_ > 0 && frame_height_ > 0) {
        const auto dest = FrameDestRect();
        renderer.DrawFrameBgra(frame_.data(), frame_width_, frame_height_,
                               dest, 1.0f);
        renderer.StrokeRect(dest, tokens.border, 1.0f, 0.0f);
        DrawOverlays(renderer, tokens, dest);
    } else {
        renderer.DrawText(
            status_text_.empty() ? "No video" : status_text_, video_area,
            {status_error_ ? tokens.danger : tokens.muted,
             tokens.font_body, 500});
    }

    DrawMeters(renderer, tokens, video_area);
    DrawTimecode(renderer, tokens, video_area);

    // Scopes overlay: semi-transparent dark backing + border, the
    // scopes widget drawn inside. The SetFrame() call refreshes the
    // source pointer every paint so mode switches on paused stills
    // recompute immediately (recompute is throttled internally).
    MonitorScopes& scopes = ScopesState(this);
    if (scopes.visible && !frame_.empty() && frame_width_ > 0 &&
        frame_height_ > 0) {
        scopes.widget.SetFrame(frame_.data(), frame_width_,
                               frame_height_, frame_width_ * 4);
        const auto overlay = ScopesOverlayRect(video_area);
        renderer.FillRect(overlay, {0.0f, 0.0f, 0.0f, 0.62f},
                          tokens.radius_small);
        renderer.StrokeRect(overlay, WithAlpha(tokens.border, 0.9f),
                            1.0f, tokens.radius_small);
        scopes.widget.Layout(overlay);
        scopes.widget.Render(renderer, tokens);
    }

    DrawRuler(renderer, tokens, RulerRect());
    DrawTransport(renderer, tokens, TransportRect());
}

void MonitorWidget::DrawOverlays(Rendering::Renderer& renderer,
                                 const XER::UI::Theme::Tokens& tokens,
                                 const Layout::Rect& dest) {
    if (dest.width <= 0.0f || dest.height <= 0.0f) return;

    if (grid_) {
        const Color line = {1.0f, 1.0f, 1.0f, 0.30f};
        for (int i = 1; i <= 2; ++i) {
            const float x = dest.x + dest.width * (i / 3.0f);
            const float y = dest.y + dest.height * (i / 3.0f);
            renderer.DrawLine({x, dest.y}, {x, dest.y + dest.height},
                              line, 1.0f);
            renderer.DrawLine({dest.x, y}, {dest.x + dest.width, y},
                              line, 1.0f);
        }
    }

    if (safe_margins_) {
        // Action-safe 90% (outer) and title-safe 80% (inner).
        const Color action_color = {1.0f, 1.0f, 1.0f, 0.55f};
        const Color title_color = WithAlpha(tokens.accent, 0.8f);
        const Layout::Rect action{
            dest.x + dest.width * 0.05f, dest.y + dest.height * 0.05f,
            dest.width * 0.90f, dest.height * 0.90f};
        const Layout::Rect title{
            dest.x + dest.width * 0.10f, dest.y + dest.height * 0.10f,
            dest.width * 0.80f, dest.height * 0.80f};
        renderer.StrokeRect(action, action_color, 1.0f, 0.0f);
        renderer.StrokeRect(title, title_color, 1.0f, 0.0f);
        // Center cross.
        const float cx = dest.x + dest.width * 0.5f;
        const float cy = dest.y + dest.height * 0.5f;
        const float arm = std::min(dest.width, dest.height) * 0.02f + 6.0f;
        renderer.DrawLine({cx - arm, cy}, {cx + arm, cy}, title_color,
                          1.0f);
        renderer.DrawLine({cx, cy - arm}, {cx, cy + arm}, title_color,
                          1.0f);
    }
}

void MonitorWidget::DrawMeters(Rendering::Renderer& renderer,
                               const XER::UI::Theme::Tokens& tokens,
                               const Layout::Rect& video_area) {
    // Two thin bars hugging the right edge of the video area.
    constexpr float kBarWidth = 5.0f;
    constexpr float kBarGap = 2.0f;
    const float top = video_area.y + 6.0f;
    const float bottom = video_area.y + video_area.height - 6.0f;
    if (bottom - top < 24.0f) return;  // too small to be useful
    const float right = video_area.x + video_area.width - 6.0f;
    const float lx = right - kBarGap - kBarWidth * 2.0f;
    const float rx = right - kBarWidth;

    const auto y_for_db = [&](float db) {
        const float u = std::clamp(
            (db - kMeterMinDb) / (0.0f - kMeterMinDb), 0.0f, 1.0f);
        return bottom - u * (bottom - top);
    };

    const auto draw_bar = [&](float x, float level_db, float peak_db) {
        // Track.
        renderer.FillRect({x, top, kBarWidth, bottom - top},
                          {0.0f, 0.0f, 0.0f, 0.55f}, 1.0f);
        const float y_level = y_for_db(level_db);
        // Green zone [min, -12] — always the segment under the level.
        const float green_top = std::max(y_level, y_for_db(kMeterYellowDb));
        if (green_top < bottom) {
            renderer.FillRect({x, green_top, kBarWidth, bottom - green_top},
                              MeterGreen(), 0.0f);
        }
        // Yellow zone (-12, -3] when the level reaches it.
        if (level_db > kMeterYellowDb) {
            const float yellow_top =
                std::max(y_level, y_for_db(kMeterRedDb));
            const float yellow_bottom = y_for_db(kMeterYellowDb);
            if (yellow_top < yellow_bottom) {
                renderer.FillRect(
                    {x, yellow_top, kBarWidth, yellow_bottom - yellow_top},
                    MeterYellow(), 0.0f);
            }
        }
        // Red zone (-3, 0].
        if (level_db > kMeterRedDb) {
            const float red_bottom = y_for_db(kMeterRedDb);
            if (y_level < red_bottom) {
                renderer.FillRect(
                    {x, y_level, kBarWidth, red_bottom - y_level},
                    MeterRed(), 0.0f);
            }
        }
        // Peak-hold mark.
        if (peak_db > kMeterMinDb + 0.5f) {
            const float y_peak = y_for_db(peak_db);
            renderer.DrawLine({x, y_peak}, {x + kBarWidth, y_peak},
                              peak_db > kMeterRedDb ? MeterRed()
                                                    : Color{1, 1, 1, 0.9f},
                              1.5f);
        }
        // Zone tick marks at -12 / -3 dB.
        const Color tick = WithAlpha(tokens.muted, 0.5f);
        renderer.DrawLine({x, y_for_db(kMeterYellowDb)},
                          {x + kBarWidth, y_for_db(kMeterYellowDb)},
                          tick, 1.0f);
        renderer.DrawLine({x, y_for_db(kMeterRedDb)},
                          {x + kBarWidth, y_for_db(kMeterRedDb)},
                          tick, 1.0f);
    };

    draw_bar(lx, meter_l_db_, peak_hold_l_db_);
    draw_bar(rx, meter_r_db_, peak_hold_r_db_);
}

void MonitorWidget::DrawTimecode(Rendering::Renderer& renderer,
                                 const XER::UI::Theme::Tokens& tokens,
                                 const Layout::Rect& video_area) {
    const std::string tc =
        FormatTimecode(time_, project_.fps_num, project_.fps_den);
    const Rendering::TextStyle style{tokens.foreground,
                                     std::min(tokens.font_title, 19.0f),
                                     700};
    const auto size = renderer.MeasureText(tc, style);
    const Layout::Rect chip{
        video_area.x + 8.0f,
        video_area.y + video_area.height - size.height - 14.0f,
        size.width + 14.0f, size.height + 8.0f};
    renderer.FillRect(chip, {0.0f, 0.0f, 0.0f, 0.55f},
                      tokens.radius_small);
    renderer.DrawText(
        tc, {chip.x + 7.0f, chip.y + 4.0f, size.width, size.height},
        style);
}

void MonitorWidget::DrawRuler(Rendering::Renderer& renderer,
                              const XER::UI::Theme::Tokens& tokens,
                              const Layout::Rect& ruler) {
    renderer.FillRect(ruler, tokens.background, tokens.radius_small);
    const double duration = project_.duration();
    if (duration <= 0.0 || ruler.width <= 0.0f) return;
    const auto x_at = [&](double t) {
        return ruler.x + static_cast<float>(t / duration) * ruler.width;
    };

    // Work-area in/out brackets.
    if (project_.work_out > project_.work_in) {
        const float x0 = x_at(project_.work_in);
        const float x1 = x_at(project_.work_out);
        renderer.FillRect({x0, ruler.y + 4.0f, x1 - x0,
                           ruler.height - 8.0f},
                          WithAlpha(tokens.accent, 0.30f), 1.0f);
        renderer.DrawLine({x0, ruler.y + 1.0f},
                          {x0, ruler.y + ruler.height - 1.0f},
                          tokens.accent, 2.0f);
        renderer.DrawLine({x1, ruler.y + 1.0f},
                          {x1, ruler.y + ruler.height - 1.0f},
                          tokens.accent, 2.0f);
    }

    // Loop range band (explicit range, else work area, else whole).
    if (loop_) {
        auto [li, lo] = EffectiveLoopRange();
        if (lo <= li) {
            li = 0.0;
            lo = duration;
        }
        li = std::clamp(li, 0.0, duration);
        lo = std::clamp(lo, 0.0, duration);
        const float x0 = x_at(li);
        const float x1 = x_at(lo);
        renderer.FillRect(
            {x0, ruler.y + ruler.height - 5.0f, x1 - x0, 4.0f},
            WithAlpha(tokens.focus, 0.65f), 1.0f);
    }

    // Marker flags.
    for (const Marker& marker : project_.markers) {
        if (marker.time < 0.0 || marker.time > duration) continue;
        const float x = x_at(marker.time);
        renderer.FillRect({x - 1.0f, ruler.y + 1.0f, 2.0f,
                           ruler.height - 2.0f},
                          MarkerColor(marker), 0.0f);
        renderer.FillRect({x, ruler.y + 1.0f, 4.0f, 4.0f},
                          MarkerColor(marker), 0.0f);
    }

    // Playhead tick.
    const float px = x_at(std::clamp(time_, 0.0, duration));
    renderer.DrawLine({px, ruler.y}, {px, ruler.y + ruler.height},
                      tokens.foreground, 1.0f);

    // Active marker name at the right end of the strip.
    if (const Marker* active = ActiveMarker();
        active != nullptr && !active->name.empty()) {
        const Rendering::TextStyle style{MarkerColor(*active),
                                         tokens.font_small, 600};
        const auto size = renderer.MeasureText(active->name, style);
        const Layout::Rect rect{
            ruler.x + ruler.width - size.width - 4.0f,
            ruler.y + (ruler.height - size.height) * 0.5f,
            size.width + 4.0f, size.height};
        renderer.FillRect(rect, {0.0f, 0.0f, 0.0f, 0.5f},
                          tokens.radius_small);
        renderer.DrawText(active->name, rect, style);
    }
}

void MonitorWidget::DrawTransport(Rendering::Renderer& renderer,
                                  const XER::UI::Theme::Tokens& tokens,
                                  const Layout::Rect& row) {
    // The row's buttons are Widgets::Button children now — only the
    // right-aligned "current / duration" readout stays drawn here.
    const std::string time_text =
        FormatTime(time_) + "  /  " + FormatTime(project_.duration());
    const Rendering::TextStyle style{tokens.muted, tokens.font_small, 500};
    const auto size = renderer.MeasureText(time_text, style);
    renderer.DrawText(
        time_text,
        {row.x + row.width - size.width, row.y + 4.0f, size.width,
         row.height},
        style);
}

bool MonitorWidget::ShowContextMenu(const Layout::Point p) {
    namespace Widgets = XER::UI::Widgets;
    if (!GetBounds().Contains(p)) return false;

    std::vector<Widgets::ContextMenu::Item> items;

    Widgets::ContextMenu::Item play;
    auto& ui_strings = AppRuntime::SharedUiStrings();
    play.label = TransportPlaying()
        ? ui_strings.Get("transport_pause", "Pause")
        : ui_strings.Get("transport_play", "Play");
    play.shortcut = "Space";
    play.action = [this] {
        if (on_play_toggle_) {
            on_play_toggle_();
        } else {
            SetPlaying(!playing_);
        }
    };
    items.push_back(std::move(play));

    Widgets::ContextMenu::Item loop;
    loop.label = "Loop Playback";
    loop.checked = loop_;
    loop.separator_before = true;
    loop.action = [this] { SetLoop(!loop_); };
    items.push_back(std::move(loop));

    Widgets::ContextMenu::Item margins;
    margins.label = "Safe Margins";
    margins.checked = safe_margins_;
    margins.separator_before = true;
    margins.action = [this] { SetSafeMargins(!safe_margins_); };
    items.push_back(std::move(margins));

    Widgets::ContextMenu::Item grid;
    grid.label = "Grid";
    grid.checked = grid_;
    grid.action = [this] { SetGrid(!grid_); };
    items.push_back(std::move(grid));

    Widgets::ContextMenu::Item focus;
    focus.label = "Toggle Monitor Focus";
    focus.separator_before = true;
    focus.action = [this] {
        if (hooks_.on_toggle_focus) hooks_.on_toggle_focus();
    };
    items.push_back(std::move(focus));

    Widgets::ContextMenu::Item scopes;
    scopes.label = "Toggle Scopes";
    scopes.checked = ScopesState(this).visible;
    scopes.action = [this] {
        MonitorScopes& s = ScopesState(this);
        s.visible = !s.visible;
        Invalidate();
    };
    items.push_back(std::move(scopes));

    // Playback-quality group: the badge (quality_tag_) is the source
    // of truth for the checkmark; the host routes the scale to
    // PreviewEngine::SetQuality via on_quality_.
    const struct {
        const char* label;
        const char* tag;
        float scale;
    } kQuality[] = {
        {"Playback Quality: Full", "Full", 1.0f},
        {"Playback Quality: Half", "1/2", 0.5f},
        {"Playback Quality: Quarter", "1/4", 0.25f},
    };
    for (const auto& q : kQuality) {
        Widgets::ContextMenu::Item quality;
        quality.label = q.label;
        quality.checked = quality_tag_ == q.tag;
        quality.separator_before = q.scale == 1.0f;
        quality.action = [this, tag = q.tag, scale = q.scale] {
            SetQualityTag(tag);
            if (on_quality_) on_quality_(scale);
        };
        items.push_back(std::move(quality));
    }

    auto menu = Widgets::ContextMenu::Create(std::move(items));
    menu->SetAnchor(p);
    ShowOverlay(menu);
    return true;
}

void MonitorWidget::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    play_btn_->Layout(OverlayButtonRect(OverlayButton::Play), tokens);
    prev_btn_->Layout(OverlayButtonRect(OverlayButton::PrevFrame),
                      tokens);
    next_btn_->Layout(OverlayButtonRect(OverlayButton::NextFrame),
                      tokens);
    safe_btn_->Layout(OverlayButtonRect(OverlayButton::SafeMargins),
                      tokens);
    grid_btn_->Layout(OverlayButtonRect(OverlayButton::Grid), tokens);
    loop_btn_->Layout(OverlayButtonRect(OverlayButton::Loop), tokens);
    focus_btn_->Layout(OverlayButtonRect(OverlayButton::Focus), tokens);
    scopes_btn_->Layout(ScopesButtonRect(TransportRect()), tokens);
    if (!quality_tag_.empty()) {
        // The chip hugs the top-right of the header band, mirroring
        // where the drawn tag used to sit.
        const float w = quality_badge_->GetPreferredSize().width;
        const float pad = tokens.spacing_medium;
        quality_badge_->Layout(
            {bounds.x + bounds.width - pad - w,
             bounds.y + pad * 0.6f + 1.0f, w, 20.0f},
            tokens);
    }
}

bool MonitorWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    EnsureChildren();
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        const auto p = event.position;
        // The transport row's Buttons own their clicks (hit-tested as
        // children before the monitor sees the event).
        // Scopes overlay: mode tabs get first pick via the widget's own
        // hit test; any other click inside the overlay is swallowed so
        // it doesn't toggle playback on the frame underneath.
        MonitorScopes& scopes = ScopesState(this);
        if (scopes.visible &&
            ScopesOverlayRect(VideoAreaRect()).Contains(p)) {
            scopes.widget.HandleEvent(event);
            return true;
        }
        // Mini ruler: click to seek.
        if (RulerRect().Contains(p)) {
            if (on_seek_request_) {
                on_seek_request_(RulerTimeAtX(p.x));
            } else {
                SetPlaying(false);
                SetTime(RulerTimeAtX(p.x), true);
                EmitTime(time_);
            }
            return true;
        }
        // Clicking the frame still toggles playback (Phase-1 behavior).
        if (VideoAreaRect().Contains(p)) {
            if (on_play_toggle_) {
                on_play_toggle_();
            } else {
                SetPlaying(!playing_);
            }
            return true;
        }
        return GetBounds().Contains(p);
    }
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Space) {
            if (on_play_toggle_) {
                on_play_toggle_();
            } else {
                SetPlaying(!playing_);
            }
            return true;
        }
        const double frame_step = project_.fps_num > 0
            ? static_cast<double>(project_.fps_den) / project_.fps_num
            : 1.0 / 30.0;
        if (event.key == XER::UI::Widgets::KeyCode::ArrowLeft ||
            event.key == XER::UI::Widgets::KeyCode::ArrowRight) {
            const int dir =
                event.key == XER::UI::Widgets::KeyCode::ArrowRight
                    ? 1 : -1;
            if (!event.is_shift && on_frame_step_) {
                on_frame_step_(dir);
            } else if (on_seek_request_) {
                on_seek_request_(std::clamp(
                    time_ + dir * (event.is_shift ? 1.0 : frame_step),
                    0.0, project_.duration()));
            } else {
                SetPlaying(false);
                SetTime(time_ +
                            dir * (event.is_shift ? 1.0 : frame_step),
                        true);
                EmitTime(time_);
            }
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Home ||
            event.key == XER::UI::Widgets::KeyCode::End) {
            const double target =
                event.key == XER::UI::Widgets::KeyCode::Home
                    ? 0.0 : project_.duration();
            if (on_seek_request_) {
                on_seek_request_(target);
            } else {
                SetPlaying(false);
                SetTime(target, true);
                EmitTime(time_);
            }
            return true;
        }
    }
    return false;
}

void MonitorWidget::Tick(double dt_seconds) {
    // --- meter ballistics: instant attack, ~20 dB/s release, peak hold ---
    {
        const float fall =
            kMeterFallDbPerSec * static_cast<float>(dt_seconds);
        const float peak_fall =
            kPeakFallDbPerSec * static_cast<float>(dt_seconds);
        const float before_l = meter_l_db_;
        const float before_r = meter_r_db_;
        const float in_l = ToDb(peak_in_l_);
        const float in_r = ToDb(peak_in_r_);
        meter_l_db_ = std::max(in_l, meter_l_db_ - fall);
        meter_r_db_ = std::max(in_r, meter_r_db_ - fall);
        if (in_l >= peak_hold_l_db_) {
            peak_hold_l_db_ = in_l;
            peak_hold_l_secs_ = kPeakHoldSeconds;
        } else if (peak_hold_l_secs_ > 0.0) {
            peak_hold_l_secs_ -= dt_seconds;
        } else {
            peak_hold_l_db_ =
                std::max(meter_l_db_, peak_hold_l_db_ - peak_fall);
        }
        if (in_r >= peak_hold_r_db_) {
            peak_hold_r_db_ = in_r;
            peak_hold_r_secs_ = kPeakHoldSeconds;
        } else if (peak_hold_r_secs_ > 0.0) {
            peak_hold_r_secs_ -= dt_seconds;
        } else {
            peak_hold_r_db_ =
                std::max(meter_r_db_, peak_hold_r_db_ - peak_fall);
        }
        if (std::abs(meter_l_db_ - before_l) > 0.05f ||
            std::abs(meter_r_db_ - before_r) > 0.05f) {
            Invalidate();
        }
    }

    if (playing_ && !external_playback_) {
        const double duration = project_.duration();
        if (duration <= 0.0 || project_.fps_num <= 0 ||
            project_.fps_den <= 0) {
            SetPlaying(false);
            return;
        }
        const double frame_interval =
            static_cast<double>(project_.fps_den) / project_.fps_num;
        time_ += dt_seconds;
        // Loop over the effective range (explicit > work area > whole).
        if (loop_) {
            auto [li, lo] = EffectiveLoopRange();
            if (lo <= li) {
                li = 0.0;
                lo = duration;
            }
            if (time_ >= lo && lo > li) {
                SetTime(li, false);
            }
        }
        if (time_ >= duration) {
            if (loop_ && duration > 0.0) {
                SetTime(0.0, false);
            } else {
                time_ = duration;
                playing_ = false;
            }
        }
        play_accumulator_ += dt_seconds;
        if (play_accumulator_ >= frame_interval ||
            !compositor_ready_ || rendered_time_ < 0.0) {
            play_accumulator_ = 0.0;
            frame_dirty_ = true;
            std::string ignored;
            RenderCurrentFrame(&ignored);
            EmitTime(time_);
        }
    } else if (frame_dirty_ && !external_playback_) {
        std::string ignored;
        RenderCurrentFrame(&ignored);
    }
}

}  // namespace XER::VideoEditor::Ui
