#include "monitor_surface.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "lab/object_ops.h"   // MaskToPolygon — mask-contour overlay
#include "rendering/renderer.h"
#include "ve_theme.h"
#include "widgets/surface.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace Layout = XER::UI::Layout;
namespace Rendering = XER::UI::Rendering;
using XER::UI::Theme::Color;
namespace W = XER::UI::Widgets;

// Bottom mini-ruler strip height (old monitor_widget RulerRect
// parity): work-area brackets + loop band + marker flags + playhead.
constexpr float kRulerHeight = 14.0f;
// Drag-vs-click discrimination (DIP).
constexpr float kDragThreshold = 4.0f;
// Double-click window for zoom-fit reset (spec §6.6 — surfaces
// synthesize their own double clicks).
constexpr double kDoubleClickSeconds = 0.4;
// Wheel zoom: one notch steps the zoom by this factor.
constexpr float kZoomStep = 1.25f;

Color WithAlpha(Color c, float alpha) {
    c.a *= alpha;
    return c;
}

Color MarkerColor(const Marker& marker) {
    return {marker.color[0] / 255.0f, marker.color[1] / 255.0f,
            marker.color[2] / 255.0f, marker.color[3] / 255.0f};
}

}  // namespace

MonitorSurface::MonitorSurface(Id id) : id_(id) {
    surface_ = std::make_shared<W::Surface>();
    surface_->SetMinSize({240.0f, 160.0f});
    surface_->SetPaintCallback(
        [this](Rendering::Renderer& renderer, const Layout::Rect& c,
               const Theme::Tokens& tokens) {
            Paint(renderer, c, tokens);
        });
    surface_->SetEventCallback(
        [this](const W::Event& e) { return OnSurfaceEvent(e); });
    AddChild(surface_);
    SetFocusable(true);
}

MonitorSurface::~MonitorSurface() = default;

std::shared_ptr<MonitorSurface> MonitorSurface::Create(Id id) {
    // shared_from_this needs established ownership — construct via
    // make_shared and finish setup here (AddChild above is fine: the
    // kit queues pending children before ownership exists).
    return std::shared_ptr<MonitorSurface>(new MonitorSurface(id));
}

// ---- hooks -----------------------------------------------------------------

void MonitorSurface::SetHooks(MonitorHooks hooks) {
    hooks_ = std::move(hooks);
}
void MonitorSurface::SetOnActivate(std::function<void()> cb) {
    hooks_.on_activate = std::move(cb);
}
void MonitorSurface::SetOnTimeChanged(
    std::function<void(double)> cb) {
    hooks_.on_time_changed = std::move(cb);
}
void MonitorSurface::SetOnClickPoint(
    std::function<void(float, float)> cb) {
    hooks_.on_click_point = std::move(cb);
}
void MonitorSurface::SetOnTogglePlay(std::function<void()> cb) {
    hooks_.on_toggle_play = std::move(cb);
}
void MonitorSurface::SetOnStatus(
    std::function<void(const std::string&, bool)> cb) {
    hooks_.on_status = std::move(cb);
}
void MonitorSurface::SetClickPointsArmed(bool armed) {
    click_points_armed_ = armed;
}
void MonitorSurface::SetOnZoomChanged(
    std::function<void(float)> cb) {
    on_zoom_changed_ = std::move(cb);
}

void MonitorSurface::Activate() {
    if (hooks_.on_activate) hooks_.on_activate();
}

void MonitorSurface::EmitStatus(const std::string& text,
                                bool is_error) {
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

// ---- frame feed ------------------------------------------------------------

void MonitorSurface::SetFrame(const std::vector<std::uint8_t>& bgra,
                              int width, int height) {
    if (bgra.empty() || width <= 0 || height <= 0) return;
    // Malformed engine frame (buffer shorter than w*h*4) — drop it
    // instead of reading out of bounds in the paint path (old
    // SetExternalFrame parity; division form avoids w*h*4 overflow).
    if (bgra.size() / 4u <
        static_cast<std::size_t>(width) *
            static_cast<std::size_t>(height)) {
        return;
    }
    frame_ = bgra;
    frame_width_ = width;
    frame_height_ = height;
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetFrameProvider(FrameProvider provider) {
    frame_provider_ = std::move(provider);
    Invalidate(VideoAreaRect());
}

bool MonitorSurface::HasFrame() const {
    return frame_width_ > 0 && frame_height_ > 0 &&
           (!frame_.empty() || frame_provider_ != nullptr);
}

void MonitorSurface::SetStatusText(std::string text, bool is_error) {
    status_text_ = std::move(text);
    status_error_ = is_error;
    Invalidate(VideoAreaRect());
}

// ---- pushed document state --------------------------------------------------

void MonitorSurface::SetTime(double seconds) {
    if (time_ == seconds) return;
    time_ = seconds;
    // Playhead tick lives in the ruler strip — region invalidate only
    // (spec §6.5).
    Invalidate(RulerRect());
}

void MonitorSurface::SetDuration(double seconds) {
    duration_ = std::max(0.0, seconds);
    Invalidate(RulerRect());
}

void MonitorSurface::SetWorkArea(double in_seconds,
                                 double out_seconds) {
    work_in_ = in_seconds;
    work_out_ = out_seconds;
    Invalidate(RulerRect());
}

void MonitorSurface::SetMarkers(const std::vector<Marker>& markers) {
    markers_ = markers;
    Invalidate(RulerRect());
}

void MonitorSurface::SetMarks(double in_seconds, double out_seconds) {
    mark_in_ = in_seconds;
    mark_out_ = out_seconds;
    Invalidate(RulerRect());
}

// ---- overlay toggles --------------------------------------------------------

void MonitorSurface::SetSafeMargins(bool on) {
    safe_margins_ = on;
    Invalidate(VideoAreaRect());
}
void MonitorSurface::SetGrid(bool on) {
    grid_ = on;
    Invalidate(VideoAreaRect());
}
void MonitorSurface::SetLoop(bool on) {
    loop_ = on;
    Invalidate(RulerRect());
}
void MonitorSurface::SetLoopRange(double in_seconds,
                                  double out_seconds) {
    loop_in_ = in_seconds;
    loop_out_ = out_seconds;
    Invalidate(RulerRect());
}

// ---- zoom / pan --------------------------------------------------------------

void MonitorSurface::SetZoom(float zoom) {
    if (zoom <= 0.0f) zoom = 1.0f;
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom_ == zoom) return;
    zoom_ = zoom;
    if (zoom_ == 1.0f) {
        view_cx_ = 0.5f;
        view_cy_ = 0.5f;
    }
    if (on_zoom_changed_) on_zoom_changed_(zoom_);
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetViewCenter(float nx, float ny) {
    view_cx_ = std::clamp(nx, 0.0f, 1.0f);
    view_cy_ = std::clamp(ny, 0.0f, 1.0f);
    Invalidate(VideoAreaRect());
}

// ---- overlay inputs ----------------------------------------------------------

void MonitorSurface::SetMarkPoints(
    std::vector<std::pair<int, int>> frame_points) {
    if (mark_points_ == frame_points) return;
    mark_points_ = std::move(frame_points);
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetMaskContour(
    std::vector<Lab::IntPoint> frame_points) {
    mask_contour_in_ = std::move(frame_points);
    Invalidate(VideoAreaRect());
}

void MonitorSurface::SetMaskOverlay(
    std::shared_ptr<const std::vector<std::uint8_t>> mask, int width,
    int height) {
    mask_overlay_ = std::move(mask);
    mask_overlay_w_ = width;
    mask_overlay_h_ = height;
    // Cached tint/contour are rebuilt lazily on the next paint.
    Invalidate(VideoAreaRect());
}

void MonitorSurface::EnsureMaskOverlayBuilt(Color accent) {
    const void* key = mask_overlay_.get();
    if (key == mask_built_for_ && built_accent_[0] == accent.r &&
        built_accent_[1] == accent.g && built_accent_[2] == accent.b &&
        built_accent_[3] == accent.a) {
        return;
    }
    mask_built_for_ = key;
    built_accent_[0] = accent.r;
    built_accent_[1] = accent.g;
    built_accent_[2] = accent.b;
    built_accent_[3] = accent.a;
    mask_bgra_.clear();
    mask_contour_.clear();
    if (mask_overlay_ == nullptr || mask_overlay_w_ <= 0 ||
        mask_overlay_h_ <= 0) {
        return;
    }
    const std::size_t count =
        static_cast<std::size_t>(mask_overlay_w_) * mask_overlay_h_;
    if (mask_overlay_->size() < count) return;
    // ~40% coverage tint in premultiplied BGRA (old parity).
    mask_bgra_.resize(count * 4);
    constexpr float kFillAlpha = 0.40f;
    for (std::size_t i = 0; i < count; ++i) {
        const int a = static_cast<int>(
            static_cast<float>(mask_overlay_->at(i)) * kFillAlpha +
            0.5f);
        std::uint8_t* px = mask_bgra_.data() + i * 4;
        px[0] = static_cast<std::uint8_t>(
            accent.b * 255.0f * a / 255.0f);
        px[1] = static_cast<std::uint8_t>(
            accent.g * 255.0f * a / 255.0f);
        px[2] = static_cast<std::uint8_t>(
            accent.r * 255.0f * a / 255.0f);
        px[3] = static_cast<std::uint8_t>(a);
    }
    // Traced boundary for a crisp edge at any zoom; re-simplify
    // coarser when the raw contour would drown the paint in DrawLine
    // calls (a noisy model mask can produce thousands of vertices).
    for (const float tol : {1.5f, 4.0f, 10.0f}) {
        mask_contour_ = Lab::MaskToPolygon(*mask_overlay_,
                                           mask_overlay_w_,
                                           mask_overlay_h_, tol);
        if (mask_contour_.size() <= 512) break;
    }
}

// ---- geometry ----------------------------------------------------------------

XER::UI::Layout::Rect MonitorSurface::VideoAreaRect() const {
    const auto b = GetBounds();
    return {b.x, b.y, b.width,
            std::max(0.0f, b.height - kRulerHeight)};
}

XER::UI::Layout::Rect MonitorSurface::RulerRect() const {
    const auto b = GetBounds();
    return {b.x, b.y + std::max(0.0f, b.height - kRulerHeight),
            b.width, std::min(kRulerHeight, b.height)};
}

XER::UI::Layout::Rect MonitorSurface::FitRect() const {
    const auto video_area = VideoAreaRect();
    if (frame_width_ <= 0 || frame_height_ <= 0 ||
        video_area.width <= 0.0f || video_area.height <= 0.0f) {
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

XER::UI::Layout::Rect MonitorSurface::FittedFrameRect() const {
    const auto fit = FitRect();
    if (zoom_ <= 1.0f) return fit;
    // Zoomed: the fit rect scaled by zoom_, positioned so the
    // frame-normalized view center lands on the video-area center.
    const auto va = VideoAreaRect();
    const float w = fit.width * zoom_;
    const float h = fit.height * zoom_;
    return {va.x + va.width * 0.5f - view_cx_ * w,
            va.y + va.height * 0.5f - view_cy_ * h, w, h};
}

bool MonitorSurface::FrameNormPoint(XER::UI::Layout::Point p,
                                    float* nx, float* ny) const {
    const auto dest = FittedFrameRect();
    if (dest.width <= 0.0f || dest.height <= 0.0f ||
        !dest.Contains(p)) {
        return false;
    }
    const float u = (p.x - dest.x) / dest.width;
    const float v = (p.y - dest.y) / dest.height;
    if (nx != nullptr) *nx = std::clamp(u, 0.0f, 1.0f);
    if (ny != nullptr) *ny = std::clamp(v, 0.0f, 1.0f);
    return true;
}

double MonitorSurface::RulerTimeAtX(float x) const {
    const auto ruler = RulerRect();
    if (ruler.width <= 0.0f || duration_ <= 0.0) return 0.0;
    const float u =
        std::clamp((x - ruler.x) / ruler.width, 0.0f, 1.0f);
    return u * duration_;
}

std::pair<double, double> MonitorSurface::EffectiveLoopRange() const {
    if (loop_out_ > loop_in_) return {loop_in_, loop_out_};
    if (work_out_ > work_in_) return {work_in_, work_out_};
    return {0.0, 0.0};
}

const Marker* MonitorSurface::ActiveMarker() const {
    const Marker* active = nullptr;
    for (const Marker& marker : markers_) {
        if (marker.time <= time_ + 0.0005) {
            active = &marker;  // sorted by time — latest match wins
        } else {
            break;
        }
    }
    return active;
}

// ---- paint -------------------------------------------------------------------

void MonitorSurface::Paint(Rendering::Renderer& renderer,
                           const Layout::Rect& content,
                           const Theme::Tokens& tokens) {
    const auto video_area = VideoAreaRect();
    (void)content;  // == GetBounds() (no content size on the kit
                    // surface — the letterbox is ours)

    // Pull path wins when installed — the provider owns "what frame
    // now" (fake providers in smoke; push path otherwise).
    Frame provided;
    const std::vector<std::uint8_t>* pixels = &frame_;
    if (frame_provider_ != nullptr) {
        provided = frame_provider_();
        if (provided.bgra != nullptr && provided.width > 0 &&
            provided.height > 0 &&
            provided.bgra->size() / 4u >=
                static_cast<std::size_t>(provided.width) *
                    static_cast<std::size_t>(provided.height)) {
            pixels = provided.bgra.get();
            frame_width_ = provided.width;
            frame_height_ = provided.height;
        }
    }

    renderer.FillRect(video_area, tokens.background,
                      tokens.radius_medium);
    renderer.StrokeRect(video_area, tokens.border, 1.0f,
                        tokens.radius_medium);

    if (pixels != nullptr && !pixels->empty() && frame_width_ > 0 &&
        frame_height_ > 0) {
        const auto dest = FittedFrameRect();
        renderer.DrawFrameBgra(pixels->data(), frame_width_,
                               frame_height_, dest, 1.0f);
        renderer.StrokeRect(dest, tokens.border, 1.0f, 0.0f);
        DrawOverlays(renderer, tokens, dest);
    } else {
        renderer.DrawText(
            status_text_.empty() ? "No video" : status_text_,
            video_area,
            {status_error_ ? tokens.danger : tokens.muted,
             tokens.font_body, 500});
    }

    DrawRuler(renderer, tokens, RulerRect());
}

void MonitorSurface::DrawOverlays(Rendering::Renderer& renderer,
                                  const Theme::Tokens& tokens,
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
        // Action-safe 90% (outer) + title-safe 80% (inner) + cross.
        const Color action_color = {1.0f, 1.0f, 1.0f, 0.55f};
        const Color title_color = WithAlpha(tokens.accent, 0.8f);
        const Layout::Rect action{
            dest.x + dest.width * 0.05f, dest.y + dest.height * 0.05f,
            dest.width * 0.90f, dest.height * 0.90f};
        const Layout::Rect title{
            dest.x + dest.width * 0.10f,
            dest.y + dest.height * 0.10f,
            dest.width * 0.80f, dest.height * 0.80f};
        renderer.StrokeRect(action, action_color, 1.0f, 0.0f);
        renderer.StrokeRect(title, title_color, 1.0f, 0.0f);
        const float cx = dest.x + dest.width * 0.5f;
        const float cy = dest.y + dest.height * 0.5f;
        const float arm =
            std::min(dest.width, dest.height) * 0.02f + 6.0f;
        renderer.DrawLine({cx - arm, cy}, {cx + arm, cy}, title_color,
                          1.0f);
        renderer.DrawLine({cx, cy - arm}, {cx, cy + arm}, title_color,
                          1.0f);
    }

    // Stored AI selection mask — tint + traced boundary, only while
    // the stored dims match the shown frame (stale masks are skipped,
    // never misapplied — same rule as the old monitor).
    if (mask_overlay_ != nullptr && frame_width_ > 0 &&
        frame_height_ > 0 && mask_overlay_w_ == frame_width_ &&
        mask_overlay_h_ == frame_height_ &&
        mask_overlay_->size() >=
            static_cast<std::size_t>(frame_width_) * frame_height_) {
        EnsureMaskOverlayBuilt(tokens.accent);
        if (!mask_bgra_.empty()) {
            renderer.DrawFrameBgra(mask_bgra_.data(), mask_overlay_w_,
                                   mask_overlay_h_, dest, 1.0f);
        }
        if (mask_contour_.size() >= 2) {
            const Color edge = WithAlpha(tokens.accent, 0.9f);
            const float sx =
                dest.width / static_cast<float>(frame_width_);
            const float sy =
                dest.height / static_cast<float>(frame_height_);
            for (std::size_t i = 0; i < mask_contour_.size(); ++i) {
                const Lab::IntPoint& a = mask_contour_[i];
                const Lab::IntPoint& b =
                    mask_contour_[(i + 1) % mask_contour_.size()];
                renderer.DrawLine(
                    {dest.x + static_cast<float>(a.x) * sx,
                     dest.y + static_cast<float>(a.y) * sy},
                    {dest.x + static_cast<float>(b.x) * sx,
                     dest.y + static_cast<float>(b.y) * sy},
                    edge, 1.0f);
            }
        }
    }

    // Explicit mask contour (hook input — no coverage fill needed).
    if (mask_contour_in_.size() >= 2 && frame_width_ > 0 &&
        frame_height_ > 0) {
        const Color edge = WithAlpha(tokens.accent, 0.9f);
        const float sx =
            dest.width / static_cast<float>(frame_width_);
        const float sy =
            dest.height / static_cast<float>(frame_height_);
        for (std::size_t i = 0; i < mask_contour_in_.size(); ++i) {
            const Lab::IntPoint& a = mask_contour_in_[i];
            const Lab::IntPoint& b =
                mask_contour_in_[(i + 1) % mask_contour_in_.size()];
            renderer.DrawLine(
                {dest.x + static_cast<float>(a.x) * sx,
                 dest.y + static_cast<float>(a.y) * sy},
                {dest.x + static_cast<float>(b.x) * sx,
                 dest.y + static_cast<float>(b.y) * sy},
                edge, 1.0f);
        }
    }

    // AI click points: frame-px markers mapped through the (possibly
    // zoomed) dest rect — accent cross + white center dot.
    if (!mark_points_.empty() && frame_width_ > 0 &&
        frame_height_ > 0) {
        const Color ring = WithAlpha(tokens.accent, 0.95f);
        const Color dot = {1.0f, 1.0f, 1.0f, 0.95f};
        for (const auto& [px, py] : mark_points_) {
            const float sx =
                dest.x + (static_cast<float>(px) + 0.5f) /
                             frame_width_ * dest.width;
            const float sy =
                dest.y + (static_cast<float>(py) + 0.5f) /
                             frame_height_ * dest.height;
            constexpr float r = 6.0f;
            renderer.DrawLine({sx - r, sy}, {sx + r, sy}, ring, 2.0f);
            renderer.DrawLine({sx, sy - r}, {sx, sy + r}, ring, 2.0f);
            renderer.FillRect({sx - 1.5f, sy - 1.5f, 3.0f, 3.0f}, dot,
                              1.0f);
        }
    }
}

void MonitorSurface::DrawRuler(Rendering::Renderer& renderer,
                               const Theme::Tokens& tokens,
                               const Layout::Rect& ruler) {
    renderer.FillRect(ruler, tokens.background, tokens.radius_small);
    if (duration_ <= 0.0 || ruler.width <= 0.0f) return;
    const auto x_at = [&](double t) {
        return ruler.x +
               static_cast<float>(t / duration_) * ruler.width;
    };

    // Work-area in/out brackets.
    if (work_out_ > work_in_) {
        const float x0 = x_at(work_in_);
        const float x1 = x_at(work_out_);
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

    // Source marks (in/out brackets) — amber, distinct from the
    // accent work area (old source-monitor parity).
    if (mark_in_ >= 0.0) {
        const float x = x_at(std::clamp(mark_in_, 0.0, duration_));
        renderer.DrawLine({x, ruler.y + 1.0f},
                          {x, ruler.y + ruler.height - 1.0f},
                          tokens.warning_chip, 2.0f);
    }
    if (mark_out_ >= 0.0) {
        const float x = x_at(std::clamp(mark_out_, 0.0, duration_));
        renderer.DrawLine({x, ruler.y + 1.0f},
                          {x, ruler.y + ruler.height - 1.0f},
                          tokens.warning_chip, 2.0f);
    }

    // Loop range band (explicit range, else work area, else whole).
    if (loop_) {
        auto [li, lo] = EffectiveLoopRange();
        if (lo <= li) {
            li = 0.0;
            lo = duration_;
        }
        li = std::clamp(li, 0.0, duration_);
        lo = std::clamp(lo, 0.0, duration_);
        const float x0 = x_at(li);
        const float x1 = x_at(lo);
        renderer.FillRect(
            {x0, ruler.y + ruler.height - 5.0f, x1 - x0, 4.0f},
            WithAlpha(tokens.focus, 0.65f), 1.0f);
    }

    // Marker flags.
    for (const Marker& marker : markers_) {
        if (marker.time < 0.0 || marker.time > duration_) continue;
        const float x = x_at(marker.time);
        renderer.FillRect({x - 1.0f, ruler.y + 1.0f, 2.0f,
                           ruler.height - 2.0f},
                          MarkerColor(marker), 0.0f);
        renderer.FillRect({x, ruler.y + 1.0f, 4.0f, 4.0f},
                          MarkerColor(marker), 0.0f);
    }

    // Playhead tick.
    const float px = x_at(std::clamp(time_, 0.0, duration_));
    renderer.DrawLine({px, ruler.y}, {px, ruler.y + ruler.height},
                      tokens.playhead, 1.0f);

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

// ---- events -------------------------------------------------------------------

bool MonitorSurface::OnSurfaceEvent(const W::Event& event) {
    using Type = W::Event::Type;
    using Btn = W::MouseButton;
    const auto p = event.position;

    if (event.type == Type::MouseDown &&
        (event.mouse_button == Btn::Left ||
         event.mouse_button == Btn::Middle)) {
        Activate();  // last-active-viewer rule — any press claims the
                     // transport scope.
        left_down_ = event.mouse_button == Btn::Left;
        drag_moved_ = false;
        down_pos_ = p;
        last_pan_pos_ = p;
        if (RulerRect().Contains(p) && event.mouse_button == Btn::Left) {
            ruler_drag_ = true;
            if (hooks_.on_time_changed) {
                hooks_.on_time_changed(RulerTimeAtX(p.x));
            }
            return true;
        }
        panning_ = VideoAreaRect().Contains(p) &&
                   (event.mouse_button == Btn::Middle || Zoomed());
        return true;
    }

    if (event.type == Type::MouseMove) {
        if (ruler_drag_ && left_down_) {
            if (hooks_.on_time_changed) {
                hooks_.on_time_changed(RulerTimeAtX(p.x));
            }
            return true;
        }
        if (left_down_ || panning_) {
            const float dx = p.x - down_pos_.x;
            const float dy = p.y - down_pos_.y;
            if (std::fabs(dx) + std::fabs(dy) > kDragThreshold) {
                drag_moved_ = true;
            }
            if (panning_ && drag_moved_) {
                // Screen-px drag -> frame-normalized view-center shift.
                const auto fit = FitRect();
                if (fit.width > 0.0f && fit.height > 0.0f) {
                    const float denom_x = fit.width * zoom_;
                    const float denom_y = fit.height * zoom_;
                    SetViewCenter(
                        view_cx_ - (p.x - last_pan_pos_.x) / denom_x,
                        view_cy_ - (p.y - last_pan_pos_.y) / denom_y);
                }
                last_pan_pos_ = p;
            }
            return true;
        }
        return false;
    }

    if (event.type == Type::MouseUp) {
        if (event.mouse_button == Btn::Left) {
            left_down_ = false;
            ruler_drag_ = false;
        }
        if (event.mouse_button == Btn::Middle ||
            event.mouse_button == Btn::Left) {
            panning_ = false;
        }
        return false;   // let the tree synthesize the Click
    }

    if (event.type == Type::MouseWheel) {
        if (!VideoAreaRect().Contains(p) || frame_width_ <= 0 ||
            frame_height_ <= 0) {
            return false;
        }
        // Zoom at the cursor: keep the frame-normalized point under
        // the cursor fixed across the zoom change.
        Activate();
        const auto dest = FittedFrameRect();
        const auto fit = FitRect();
        const float nx = dest.width > 0.0f
            ? (p.x - dest.x) / dest.width : 0.5f;
        const float ny = dest.height > 0.0f
            ? (p.y - dest.y) / dest.height : 0.5f;
        const int steps = event.wheel_delta / 120;
        float zoom = zoom_;
        for (int i = 0; i < std::abs(steps); ++i) {
            zoom *= steps > 0 ? kZoomStep : 1.0f / kZoomStep;
        }
        zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
        if (zoom == zoom_) return true;
        zoom_ = zoom;
        if (zoom_ <= 1.0f) {
            view_cx_ = 0.5f;
            view_cy_ = 0.5f;
        } else {
            // Solve for the view center that keeps (nx,ny) under p.
            const auto va = VideoAreaRect();
            const float w = fit.width * zoom_;
            const float h = fit.height * zoom_;
            SetViewCenter(
                (va.x + va.width * 0.5f - p.x) / w + nx,
                (va.y + va.height * 0.5f - p.y) / h + ny);
        }
        if (on_zoom_changed_) on_zoom_changed_(zoom_);
        Invalidate(VideoAreaRect());
        return true;
    }

    if (event.type == Type::Click && event.mouse_button == Btn::Left) {
        if (drag_moved_) {
            drag_moved_ = false;   // a pan/ruler scrub, not a click
            return true;
        }
        if (RulerRect().Contains(p)) {
            if (hooks_.on_time_changed) {
                hooks_.on_time_changed(RulerTimeAtX(p.x));
            }
            return true;
        }
        // Inside the fitted frame: double-click resets the zoom;
        // armed click points report normalized frame coords;
        // otherwise the click toggles playback (old parity).
        float nx = 0.0f, ny = 0.0f;
        const bool in_frame = FrameNormPoint(p, &nx, &ny);
        if (in_frame) {
            if (click_clock_ - last_frame_click_at_ <=
                kDoubleClickSeconds) {
                last_frame_click_at_ = -10.0;
                ZoomFit();
                return true;
            }
            last_frame_click_at_ = click_clock_;
            if (click_points_armed_ && hooks_.on_click_point) {
                hooks_.on_click_point(nx, ny);
                return true;
            }
            if (hooks_.on_toggle_play) {
                hooks_.on_toggle_play();
                return true;
            }
        }
        // Presses elsewhere in the video area still claim the widget.
        return GetBounds().Contains(p);
    }

    if (event.type == Type::Click &&
        event.mouse_button == Btn::Right) {
        // Chrome-level menus live on the panel — let the event bubble
        // so the tree's context-menu handler can take it.
        return false;
    }

    return false;
}

// ---- widget contract -----------------------------------------------------------

void MonitorSurface::Render(Rendering::Renderer& renderer,
                            const Theme::Tokens& tokens) {
    // The kit Surface child paints edge-to-edge (frame + overlays +
    // ruler); nothing of ours sits behind it.
    (void)renderer;
    (void)tokens;
}

void MonitorSurface::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (surface_ != nullptr) {
        surface_->Layout(GetBounds(), CachedLayoutTokens());
    }
}

bool MonitorSurface::HandleEvent(const W::Event& event) {
    // The Surface child covers the whole bounds, so its event
    // callback sees every hit; presses landing on us directly still
    // claim the viewer scope (belt & suspenders — a press can arrive
    // before the child exists in edge layouts).
    if (event.type == W::Event::Type::MouseDown) {
        Activate();
        return false;   // let the child's callback handle geometry
    }
    return false;
}

void MonitorSurface::Tick(double dt_seconds) {
    click_clock_ += dt_seconds;
}

}  // namespace XER::VideoEditor::Ui2
