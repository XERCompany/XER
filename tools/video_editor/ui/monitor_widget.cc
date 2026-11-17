#include "monitor_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

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
    compositor_.Cancel();
    compositor_.Shutdown();
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
    frame_ = bgra;
    frame_width_ = width;
    frame_height_ = height;
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
    const float widths[] = {34.0f, 52.0f, 52.0f, 52.0f};
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
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);

    const float pad = tokens.spacing_medium;
    const Layout::Rect header{bounds.x + pad, bounds.y + pad * 0.6f,
                              bounds.width - pad * 2.0f, 24.0f};
    renderer.DrawText("PROGRAM", header,
                      {tokens.muted, tokens.font_small, 700});

    // Preview-quality badge ("Full"/"1/2"/"1/4"...) top-right.
    if (!quality_tag_.empty()) {
        const Rendering::TextStyle tag_style{tokens.accent_foreground,
                                             tokens.font_small, 600};
        const auto tag_size = renderer.MeasureText(quality_tag_, tag_style);
        const float tag_w = tag_size.width + 14.0f;
        const Layout::Rect tag{bounds.x + bounds.width - pad - tag_w,
                               bounds.y + pad * 0.6f + 1.0f, tag_w, 20.0f};
        renderer.FillRect(tag, tokens.accent, tokens.radius_small);
        renderer.DrawText(quality_tag_, tag, tag_style);
    }

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
    DrawOverlayButton(renderer, tokens, OverlayButton::Play, nullptr,
                      playing_);
    DrawOverlayButton(renderer, tokens, OverlayButton::SafeMargins,
                      "SAFE", safe_margins_);
    DrawOverlayButton(renderer, tokens, OverlayButton::Grid, "GRID",
                      grid_);
    DrawOverlayButton(renderer, tokens, OverlayButton::Loop, "LOOP",
                      loop_);

    // "current / duration" small readout, right-aligned.
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

void MonitorWidget::DrawOverlayButton(Rendering::Renderer& renderer,
                                      const XER::UI::Theme::Tokens& tokens,
                                      OverlayButton button,
                                      const char* label, bool active) {
    const auto rect = OverlayButtonRect(button);
    const Color fill = active ? tokens.accent : tokens.secondary;
    const Color text_color =
        active ? tokens.accent_foreground : tokens.muted;
    renderer.FillRect(rect, fill, tokens.radius_small);
    if (active) {
        renderer.StrokeRect(rect, tokens.accent, 1.0f,
                            tokens.radius_small);
    }
    if (button == OverlayButton::Play) {
        // Vector play/pause glyph.
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        if (playing_) {
            renderer.FillRect({cx - 5.0f, cy - 5.0f, 3.5f, 10.0f},
                              text_color, 0.5f);
            renderer.FillRect({cx + 1.5f, cy - 5.0f, 3.5f, 10.0f},
                              text_color, 0.5f);
        } else {
            renderer.DrawLine({cx - 4.0f, cy - 6.0f}, {cx + 5.0f, cy},
                              text_color, 1.6f);
            renderer.DrawLine({cx + 5.0f, cy}, {cx - 4.0f, cy + 6.0f},
                              text_color, 1.6f);
            renderer.DrawLine({cx - 4.0f, cy + 6.0f},
                              {cx - 4.0f, cy - 6.0f}, text_color, 1.6f);
        }
        return;
    }
    if (label != nullptr) {
        renderer.DrawText(label, rect,
                          {text_color, tokens.font_small, 600});
    }
}

void MonitorWidget::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool MonitorWidget::HandleEvent(const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        const auto p = event.position;
        // Transport buttons first.
        if (OverlayButtonRect(OverlayButton::Play).Contains(p)) {
            SetPlaying(!playing_);
            return true;
        }
        if (OverlayButtonRect(OverlayButton::SafeMargins).Contains(p)) {
            SetSafeMargins(!safe_margins_);
            return true;
        }
        if (OverlayButtonRect(OverlayButton::Grid).Contains(p)) {
            SetGrid(!grid_);
            return true;
        }
        if (OverlayButtonRect(OverlayButton::Loop).Contains(p)) {
            SetLoop(!loop_);
            return true;
        }
        // Mini ruler: click to seek.
        if (RulerRect().Contains(p)) {
            SetPlaying(false);
            SetTime(RulerTimeAtX(p.x), true);
            EmitTime(time_);
            return true;
        }
        // Clicking the frame still toggles playback (Phase-1 behavior).
        if (VideoAreaRect().Contains(p)) {
            SetPlaying(!playing_);
            return true;
        }
        return GetBounds().Contains(p);
    }
    if (event.type == Event::Type::KeyDown) {
        if (event.key == XER::UI::Widgets::KeyCode::Space) {
            SetPlaying(!playing_);
            return true;
        }
        const double frame_step = project_.fps_num > 0
            ? static_cast<double>(project_.fps_den) / project_.fps_num
            : 1.0 / 30.0;
        if (event.key == XER::UI::Widgets::KeyCode::ArrowLeft) {
            SetPlaying(false);
            SetTime(time_ - (event.is_shift ? 1.0 : frame_step), true);
            EmitTime(time_);
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::ArrowRight) {
            SetPlaying(false);
            SetTime(time_ + (event.is_shift ? 1.0 : frame_step), true);
            EmitTime(time_);
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::Home) {
            SetPlaying(false);
            SetTime(0.0, true);
            EmitTime(time_);
            return true;
        }
        if (event.key == XER::UI::Widgets::KeyCode::End) {
            SetPlaying(false);
            SetTime(project_.duration(), true);
            EmitTime(time_);
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
