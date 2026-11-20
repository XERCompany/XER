// source_monitor_widget.cc — see the header for the contract. Layout
// and chrome deliberately mirror MonitorWidget (program monitor) so
// the two sit side by side without visual drift.
#include "source_monitor_widget.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iterator>

#include "app_runtime/app_runtime.h"
#include "app_runtime/icon_pack.h"
#include "app_runtime/ui_manifest.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::VideoEditor::Ui {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;

Color WithAlpha(Color c, float alpha) {
    c.a = alpha;
    return c;
}

// Theme float color -> packed AARRGGBB tint for the icon pack (same
// helper the program monitor uses).
std::uint32_t ToArgb(Color color) {
    const auto byte = [](float v) {
        return static_cast<std::uint32_t>(
            std::clamp(std::lround(v * 255.0f), 0L, 255L));
    };
    return (byte(color.a) << 24) | (byte(color.r) << 16) |
           (byte(color.g) << 8) | byte(color.b);
}

bool DrawIcon(Rendering::Renderer& renderer, const char* name, int px,
              Color tint, const Layout::Rect& box) {
    const auto* bmp =
        AppRuntime::SharedIconPack().Get(name, px, ToArgb(tint));
    if (bmp == nullptr) return false;
    const Layout::Rect dst{box.x + (box.width - bmp->width) * 0.5f,
                           box.y + (box.height - bmp->height) * 0.5f,
                           static_cast<float>(bmp->width),
                           static_cast<float>(bmp->height)};
    return renderer.DrawFrameBgra(bmp->bgra.data(), bmp->width,
                                  bmp->height, dst, 1.0f);
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

// Big monitor timecode HH:MM:SS:FF — identical convention to the
// program monitor (frame index inside the current second at the
// source's nominal rate).
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

SourceMonitorWidget::SourceMonitorWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetTooltip("Source monitor — double-click a bin clip to load; "
               "I/O mark, ,/. insert/overwrite");
}

void SourceMonitorWidget::SetSource(const std::string& path,
                                    const std::string& name,
                                    double duration, int fps_num,
                                    int fps_den, bool has_video) {
    path_ = path;
    name_ = name;
    duration_ = std::max(0.0, duration);
    fps_num_ = fps_num > 0 ? fps_num : 30;
    fps_den_ = fps_den > 0 ? fps_den : 1;
    has_video_ = has_video;
    time_ = 0.0;
    playing_ = false;
    mark_in_ = -1.0;
    mark_out_ = -1.0;
    frame_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    status_error_ = false;
    status_text_ = has_video_ ? "Decoding…"
                              : "Audio media — no video";
    Invalidate();
}

void SourceMonitorWidget::ClearSource() {
    path_.clear();
    name_.clear();
    duration_ = 0.0;
    time_ = 0.0;
    playing_ = false;
    mark_in_ = -1.0;
    mark_out_ = -1.0;
    frame_.clear();
    frame_width_ = 0;
    frame_height_ = 0;
    status_error_ = false;
    status_text_ = "No clip loaded";
    Invalidate();
}

void SourceMonitorWidget::SetFrame(
    const std::vector<std::uint8_t>& bgra, int width, int height) {
    if (bgra.empty() || width <= 0 || height <= 0) return;
    if (bgra.size() / 4u < static_cast<std::size_t>(width) *
                               static_cast<std::size_t>(height)) {
        return;   // malformed buffer — never blit out of bounds
    }
    frame_ = bgra;
    frame_width_ = width;
    frame_height_ = height;
    Invalidate();
}

void SourceMonitorWidget::SetTime(double seconds) {
    const double t = std::clamp(
        seconds, 0.0, duration_ > 0.0 ? duration_ : seconds);
    if (t == time_) return;
    time_ = t;
    Invalidate();
}

void SourceMonitorWidget::SetPlaying(bool playing) {
    if (playing_ == playing) return;
    playing_ = playing;
    Invalidate();
}

void SourceMonitorWidget::SetMarks(double in_seconds,
                                   double out_seconds) {
    mark_in_ = in_seconds;
    mark_out_ = out_seconds;
    Invalidate();
}

void SourceMonitorWidget::SetStatusText(const std::string& text,
                                        bool is_error) {
    status_text_ = text;
    status_error_ = is_error;
    if (hooks_.on_status) hooks_.on_status(text, is_error);
    Invalidate();
}

// ---- geometry ----

Layout::Rect SourceMonitorWidget::VideoAreaRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const float chrome_bottom =
        16.0f + 2.0f +   // ruler strip + gap
        28.0f +          // transport row + gap
        20.0f +          // footer readout
        pad;
    return {bounds.x + pad, bounds.y + 34.0f,
            bounds.width - pad * 2.0f,
            std::max(60.0f, bounds.height - 34.0f - chrome_bottom)};
}

Layout::Rect SourceMonitorWidget::RulerRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const auto video = VideoAreaRect();
    return {bounds.x + pad, video.y + video.height + 2.0f,
            bounds.width - pad * 2.0f, 14.0f};
}

Layout::Rect SourceMonitorWidget::TransportRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const auto ruler = RulerRect();
    return {bounds.x + pad, ruler.y + ruler.height + 2.0f,
            bounds.width - pad * 2.0f, 26.0f};
}

Layout::Rect SourceMonitorWidget::FooterRect() const {
    const auto bounds = GetBounds();
    const float pad = CachedLayoutTokens().spacing_medium;
    const auto row = TransportRect();
    return {bounds.x + pad, row.y + row.height + 2.0f,
            bounds.width - pad * 2.0f, 16.0f};
}

Layout::Rect SourceMonitorWidget::ButtonRect(Button button) const {
    const auto row = TransportRect();
    const float gap = 6.0f;
    // Play |◀ ▶| then the edit cluster: marks, then the two
    // three-point-edit buttons (Premiere , and .).
    const float widths[] = {34.0f, 30.0f, 30.0f, 44.0f, 44.0f, 52.0f,
                            52.0f};
    float x = row.x;
    if (AppRuntime::SharedLayoutConfig().transport_center) {
        float total = gap * (std::size(widths) - 1);
        for (float w : widths) total += w;
        x = row.x + std::max(0.0f, (row.width - total) * 0.5f);
    }
    const int index = static_cast<int>(button);
    for (int i = 0; i < index; ++i) x += widths[i] + gap;
    return {x, row.y, widths[index], row.height};
}

Layout::Rect SourceMonitorWidget::FrameDestRect() const {
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

double SourceMonitorWidget::RulerTimeAtX(float x) const {
    const auto ruler = RulerRect();
    if (ruler.width <= 0.0f || duration_ <= 0.0) return 0.0;
    const float u = std::clamp((x - ruler.x) / ruler.width, 0.0f, 1.0f);
    return u * duration_;
}

void SourceMonitorWidget::EmitSeek(double t) {
    if (hooks_.on_seek_request) {
        hooks_.on_seek_request(std::clamp(t, 0.0, duration_));
    }
}

// ---- render ----

void SourceMonitorWidget::Render(Rendering::Renderer& renderer,
                                 const XER::UI::Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f,
                        tokens.radius_large);

    const float pad = tokens.spacing_medium;
    const Layout::Rect header{bounds.x + pad, bounds.y + pad * 0.6f,
                              bounds.width - pad * 2.0f, 24.0f};
    // assets/ui/strings.ini — source_title overrides the header.
    const std::string title =
        name_.empty()
            ? AppRuntime::SharedUiStrings().Get("source_title",
                                                "SOURCE")
            : AppRuntime::SharedUiStrings().Get("source_title",
                                                "SOURCE") +
                  " · " + name_;
    renderer.DrawText(title, header,
                      {tokens.muted, tokens.font_small, 700});

    const auto video_area = VideoAreaRect();
    renderer.FillRect(video_area, tokens.background,
                      tokens.radius_medium);
    renderer.StrokeRect(video_area, tokens.border, 1.0f,
                        tokens.radius_medium);

    if (!frame_.empty() && frame_width_ > 0 && frame_height_ > 0) {
        const auto dest = FrameDestRect();
        renderer.DrawFrameBgra(frame_.data(), frame_width_,
                               frame_height_, dest, 1.0f);
        renderer.StrokeRect(dest, tokens.border, 1.0f, 0.0f);
    } else {
        renderer.DrawText(status_text_, video_area,
                          {status_error_ ? tokens.danger : tokens.muted,
                           tokens.font_body, 500});
    }

    // Source timecode chip (bottom-left), same convention as the
    // program monitor.
    if (!path_.empty()) {
        const std::string tc =
            FormatTimecode(time_, fps_num_, fps_den_);
        const Rendering::TextStyle style{
            tokens.foreground, std::min(tokens.font_title, 19.0f), 700};
        const auto size = renderer.MeasureText(tc, style);
        const Layout::Rect chip{
            video_area.x + 8.0f,
            video_area.y + video_area.height - size.height - 14.0f,
            size.width + 14.0f, size.height + 8.0f};
        renderer.FillRect(chip, {0.0f, 0.0f, 0.0f, 0.55f},
                          tokens.radius_small);
        renderer.DrawText(
            tc, {chip.x + 7.0f, chip.y + 4.0f, size.width,
                 size.height},
            style);
    }

    DrawRuler(renderer, tokens, RulerRect());
    DrawTransport(renderer, tokens, TransportRect());
    DrawFooter(renderer, tokens, FooterRect());
    if (IsFocused()) {
        // Focus ring: the same affordance other widgets get from the
        // shared RenderFocusRing helper — signals where I/O/JKL land.
        RenderFocusRing(renderer, tokens, tokens.radius_large);
    }
}

void SourceMonitorWidget::DrawRuler(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Layout::Rect& ruler) {
    renderer.FillRect(ruler, tokens.background, tokens.radius_small);
    if (duration_ <= 0.0 || ruler.width <= 0.0f) return;
    const auto x_at = [&](double t) {
        return ruler.x +
               static_cast<float>(t / duration_) * ruler.width;
    };

    // Marked in/out brackets — the range band plus solid edge ticks.
    const double range_in = mark_in_ >= 0.0 ? mark_in_ : 0.0;
    const double range_out =
        mark_out_ > range_in ? mark_out_ : duration_;
    if (mark_in_ >= 0.0 || mark_out_ > range_in) {
        const float x0 = x_at(std::clamp(range_in, 0.0, duration_));
        const float x1 = x_at(std::clamp(range_out, 0.0, duration_));
        renderer.FillRect({x0, ruler.y + 4.0f, x1 - x0,
                           ruler.height - 8.0f},
                          WithAlpha(tokens.accent, 0.30f), 1.0f);
        if (mark_in_ >= 0.0) {
            renderer.DrawLine({x0, ruler.y + 1.0f},
                              {x0, ruler.y + ruler.height - 1.0f},
                              tokens.accent, 2.0f);
        }
        if (mark_out_ > range_in) {
            renderer.DrawLine({x1, ruler.y + 1.0f},
                              {x1, ruler.y + ruler.height - 1.0f},
                              tokens.accent, 2.0f);
        }
    }

    // Playhead tick.
    const float px = x_at(std::clamp(time_, 0.0, duration_));
    renderer.DrawLine({px, ruler.y}, {px, ruler.y + ruler.height},
                      tokens.foreground, 1.0f);
}

void SourceMonitorWidget::DrawTransport(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Layout::Rect& row) {
    DrawButton(renderer, tokens, Button::Play, nullptr, playing_);
    DrawButton(renderer, tokens, Button::PrevFrame, nullptr, false);
    DrawButton(renderer, tokens, Button::NextFrame, nullptr, false);
    DrawButton(renderer, tokens, Button::MarkIn, "{ IN",
               mark_in_ >= 0.0);
    DrawButton(renderer, tokens, Button::MarkOut, "OUT }",
               mark_out_ > (mark_in_ >= 0.0 ? mark_in_ : 0.0));
    DrawButton(renderer, tokens, Button::Insert, "INS ,", false);
    DrawButton(renderer, tokens, Button::Overwrite, "OVR .", false);

    // "current / duration" readout, right-aligned (mirrors the
    // program monitor's transport readout).
    const std::string time_text =
        FormatTime(time_) + "  /  " + FormatTime(duration_);
    const Rendering::TextStyle style{tokens.muted, tokens.font_small,
                                     500};
    const auto size = renderer.MeasureText(time_text, style);
    renderer.DrawText(
        time_text,
        {row.x + row.width - size.width, row.y + 4.0f, size.width,
         row.height},
        style);
}

void SourceMonitorWidget::DrawButton(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens, Button button,
    const char* label, bool active) {
    const auto rect = ButtonRect(button);
    const bool enabled = HasSource();
    const Color fill = active ? tokens.accent : tokens.secondary;
    const Color text_color =
        !enabled ? WithAlpha(tokens.muted, 0.4f)
                 : (active ? tokens.accent_foreground : tokens.muted);
    renderer.FillRect(rect, fill, tokens.radius_small);
    if (active) {
        renderer.StrokeRect(rect, tokens.accent, 1.0f,
                            tokens.radius_small);
    }
    const char* icon_name = nullptr;
    switch (button) {
        case Button::Play:
            icon_name = playing_ ? "transport.pause"
                                 : "transport.play";
            break;
        case Button::PrevFrame:
            icon_name = "transport.prev";
            break;
        case Button::NextFrame:
            icon_name = "transport.next";
            break;
        default:
            break;
    }
    if (icon_name != nullptr &&
        DrawIcon(renderer, icon_name, 16, text_color, rect)) {
        return;
    }
    if (button == Button::Play) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        if (playing_) {
            renderer.FillRect({cx - 5.0f, cy - 5.0f, 3.5f, 10.0f},
                              text_color, 0.5f);
            renderer.FillRect({cx + 1.5f, cy - 5.0f, 3.5f, 10.0f},
                              text_color, 0.5f);
        } else {
            renderer.DrawLine({cx - 4.0f, cy - 6.0f},
                              {cx + 5.0f, cy}, text_color, 1.6f);
            renderer.DrawLine({cx + 5.0f, cy},
                              {cx - 4.0f, cy + 6.0f}, text_color, 1.6f);
            renderer.DrawLine({cx - 4.0f, cy + 6.0f},
                              {cx - 4.0f, cy - 6.0f}, text_color, 1.6f);
        }
        return;
    }
    if (button == Button::PrevFrame || button == Button::NextFrame) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const int dir = button == Button::NextFrame ? 1 : -1;
        const float bar_x = cx - dir * 5.5f;
        renderer.FillRect({bar_x, cy - 6.0f, 1.8f, 12.0f}, text_color,
                          0.5f);
        renderer.DrawLine({cx + dir * 4.0f, cy - 5.5f},
                          {cx - dir * 3.0f, cy}, text_color, 1.5f);
        renderer.DrawLine({cx - dir * 3.0f, cy},
                          {cx + dir * 4.0f, cy + 5.5f}, text_color,
                          1.5f);
        renderer.DrawLine({cx + dir * 4.0f, cy + 5.5f},
                          {cx + dir * 4.0f, cy - 5.5f}, text_color,
                          1.5f);
        return;
    }
    if (label != nullptr) {
        renderer.DrawText(label, rect,
                          {text_color, tokens.font_small, 600});
    }
}

void SourceMonitorWidget::DrawFooter(
    Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens,
    const Layout::Rect& row) {
    // Source timecode + marked range — the "status/footer" readout the
    // spec calls for ("00:00:01:10 — 00:00:04:20 · 3.1s marked" or the
    // full-media range when nothing is marked).
    std::string text;
    if (path_.empty()) {
        text = "Double-click a bin clip to load it";
    } else {
        const double range_in = mark_in_ >= 0.0 ? mark_in_ : 0.0;
        const double range_out =
            mark_out_ > range_in ? mark_out_ : duration_;
        char buffer[96];
        const bool marked = mark_in_ >= 0.0 || mark_out_ > range_in;
        std::snprintf(
            buffer, sizeof(buffer), "%s  —  %s   ·   %.2fs%s",
            FormatTimecode(range_in, fps_num_, fps_den_).c_str(),
            FormatTimecode(range_out, fps_num_, fps_den_).c_str(),
            std::max(0.0, range_out - range_in),
            marked ? " marked" : " (unmarked)");
        text = buffer;
    }
    renderer.DrawText(text, row, {tokens.muted, tokens.font_small, 400});
}

// ---- events ----

void SourceMonitorWidget::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool SourceMonitorWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    using XER::UI::Widgets::Event;
    using XER::UI::Widgets::MouseButton;

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left) {
        if (!GetBounds().Contains(event.position)) return false;
        if (hooks_.on_activate) hooks_.on_activate();
        // Ruler press starts a scrub — mouse capture keeps MouseMove
        // routed here for the drag.
        if (RulerRect().Contains(event.position)) {
            ruler_drag_ = true;
            EmitSeek(RulerTimeAtX(event.position.x));
        }
        return true;
    }
    if (event.type == Event::Type::MouseMove && ruler_drag_) {
        EmitSeek(RulerTimeAtX(event.position.x));
        return true;
    }
    if (event.type == Event::Type::MouseUp &&
        event.mouse_button == MouseButton::Left) {
        if (ruler_drag_) {
            ruler_drag_ = false;
            EmitSeek(RulerTimeAtX(event.position.x));
            return true;
        }
        return GetBounds().Contains(event.position);
    }
    if (event.type == Event::Type::Click &&
        event.mouse_button == MouseButton::Left) {
        const auto p = event.position;
        if (!HasSource()) {
            return GetBounds().Contains(p);
        }
        if (ButtonRect(Button::Play).Contains(p)) {
            if (hooks_.on_play_toggle) hooks_.on_play_toggle();
            return true;
        }
        if (ButtonRect(Button::PrevFrame).Contains(p) ||
            ButtonRect(Button::NextFrame).Contains(p)) {
            const int dir = ButtonRect(Button::NextFrame).Contains(p)
                                ? 1
                                : -1;
            if (hooks_.on_frame_step) hooks_.on_frame_step(dir);
            return true;
        }
        if (ButtonRect(Button::MarkIn).Contains(p)) {
            if (hooks_.on_mark_in) hooks_.on_mark_in();
            return true;
        }
        if (ButtonRect(Button::MarkOut).Contains(p)) {
            if (hooks_.on_mark_out) hooks_.on_mark_out();
            return true;
        }
        if (ButtonRect(Button::Insert).Contains(p)) {
            if (hooks_.on_insert) hooks_.on_insert();
            return true;
        }
        if (ButtonRect(Button::Overwrite).Contains(p)) {
            if (hooks_.on_overwrite) hooks_.on_overwrite();
            return true;
        }
        // Clicking the frame toggles playback like the program
        // monitor's video area.
        if (VideoAreaRect().Contains(p)) {
            if (hooks_.on_play_toggle) hooks_.on_play_toggle();
            return true;
        }
        return GetBounds().Contains(p);
    }
    // Mouse wheel/move inside the panel is consumed but unhandled —
    // key events must return false so the app's global shortcuts
    // (I/O marks, JKL shuttle, ,/. edits) still fire while this
    // monitor holds focus.
    if (event.type == Event::Type::MouseMove ||
        event.type == Event::Type::MouseWheel) {
        return GetBounds().Contains(event.position);
    }
    return false;
}

void SourceMonitorWidget::Tick(double dt_seconds) {
    (void)dt_seconds;   // frames arrive via SetFrame — nothing to step
}

}  // namespace XER::VideoEditor::Ui
