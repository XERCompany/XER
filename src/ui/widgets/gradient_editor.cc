// See gradient_editor.h — stop editor + type tabs + angle dial.
#include "gradient_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "colorspace/oklch.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

namespace CS = XER::UI::ColorSpace;

constexpr float kPad = 10.0f;
constexpr float kGap = 8.0f;
constexpr float kTabsHeight = 24.0f;
constexpr float kPreviewHeight = 44.0f;
constexpr float kStopBarHeight = 30.0f;
constexpr float kTrackHeight = 10.0f;
constexpr float kHandleSize = 15.0f;
constexpr float kHandleHit = 11.0f;
constexpr float kAngleRowHeight = 44.0f;
constexpr float kDialSize = 40.0f;
constexpr float kDragOutDistance = 26.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr int kPreviewRasterMaxW = 256;
constexpr int kPreviewRasterMaxH = 64;

const char* kTypeLabels[] = {"Linear", "Radial", "Conic"};

Theme::Color WithAlpha(Theme::Color c, float a) {
    c.a *= a;
    return c;
}

Theme::Color Lerp(Theme::Color a, Theme::Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// Shortest-arc hue interpolation (CSS "shorter hue" — the default for
// polar color interpolation).
float LerpHue(float a, float b, float t) {
    float d = CS::WrapHue(b) - CS::WrapHue(a);
    if (d > 180.0f) d -= 360.0f;
    if (d < -180.0f) d += 360.0f;
    return CS::WrapHue(a + d * t);
}

void CheckerBgra(Theme::Color color, int x, int y, std::uint8_t* px) {
    const bool dark = ((x / 4) + (y / 4)) % 2 == 0;
    const float bg = dark ? 0.42f : 0.72f;
    color = CS::ClampSrgb(color);
    const float a = color.a;
    px[0] = static_cast<std::uint8_t>(
        (color.b * a + bg * (1.0f - a)) * 255.0f + 0.5f);
    px[1] = static_cast<std::uint8_t>(
        (color.g * a + bg * (1.0f - a)) * 255.0f + 0.5f);
    px[2] = static_cast<std::uint8_t>(
        (color.r * a + bg * (1.0f - a)) * 255.0f + 0.5f);
    px[3] = 255;
}

} // namespace

GradientEditor::GradientEditor() {
    SetFocusable(true);
    // Sensible default: a two-stop accent-ish ramp so a fresh editor shows
    // something editable rather than a blank band.
    gradient_.stops = {
        {NextId(), {0.2f, 0.4f, 0.9f, 1.0f}, 0.0f},
        {NextId(), {0.6f, 0.2f, 0.7f, 1.0f}, 1.0f},
    };
}

void GradientEditor::SetGradient(Gradient gradient) {
    gradient_ = std::move(gradient);
    // Keep ids unique and the counter ahead of every assigned id — hosts
    // may hand us stops minted elsewhere.
    std::uint32_t max_id = 0;
    for (const auto& s : gradient_.stops) {
        max_id = std::max(max_id, s.id);
    }
    next_id_ = std::max(next_id_, max_id + 1);
    for (auto& s : gradient_.stops) {
        if (s.id == 0) {
            s.id = NextId();
        }
        s.position = std::clamp(s.position, 0.0f, 1.0f);
    }
    std::sort(gradient_.stops.begin(), gradient_.stops.end(),
              [](const GradientStop& a, const GradientStop& b) {
                  return a.position < b.position;
              });
    preview_dirty_ = true;
    Invalidate();
}

void GradientEditor::SetType(GradientType type) {
    if (gradient_.type == type) {
        return;
    }
    gradient_.type = type;
    preview_dirty_ = true;
    Invalidate();
    ApplyEdit(false);
    Commit();
}

void GradientEditor::SetAngle(float degrees) {
    const float a = CS::WrapHue(degrees);
    if (a == gradient_.angle_deg) {
        return;
    }
    gradient_.angle_deg = a;
    preview_dirty_ = true;
    Invalidate();
}

void GradientEditor::SetInterpolation(Interpolation interp) {
    if (interp_ == interp) {
        return;
    }
    interp_ = interp;
    preview_dirty_ = true;
    Invalidate();
}

std::uint32_t GradientEditor::AddStop(Theme::Color color, float position) {
    GradientStop stop;
    stop.id = NextId();
    stop.color = color;
    stop.position = std::clamp(position, 0.0f, 1.0f);
    auto it = std::lower_bound(
        gradient_.stops.begin(), gradient_.stops.end(), stop.position,
        [](const GradientStop& s, float pos) { return s.position < pos; });
    gradient_.stops.insert(it, stop);
    SelectInternal(stop.id);
    preview_dirty_ = true;
    Invalidate();
    return stop.id;
}

bool GradientEditor::RemoveStop(std::uint32_t id) {
    // Two stops is the smallest meaningful gradient.
    if (gradient_.stops.size() <= 2) {
        return false;
    }
    const auto it = std::find_if(gradient_.stops.begin(),
                                 gradient_.stops.end(),
                                 [id](const GradientStop& s) {
                                     return s.id == id;
                                 });
    if (it == gradient_.stops.end()) {
        return false;
    }
    gradient_.stops.erase(it);
    if (selected_ == id) {
        SelectInternal(std::nullopt);
    }
    preview_dirty_ = true;
    Invalidate();
    return true;
}

bool GradientEditor::SetStopColor(std::uint32_t id, Theme::Color color) {
    const auto it = std::find_if(gradient_.stops.begin(),
                                 gradient_.stops.end(),
                                 [id](const GradientStop& s) {
                                     return s.id == id;
                                 });
    if (it == gradient_.stops.end()) {
        return false;
    }
    it->color = color;
    preview_dirty_ = true;
    Invalidate();
    return true;
}

bool GradientEditor::SetStopPosition(std::uint32_t id, float position) {
    const auto it = std::find_if(gradient_.stops.begin(),
                                 gradient_.stops.end(),
                                 [id](const GradientStop& s) {
                                     return s.id == id;
                                 });
    if (it == gradient_.stops.end()) {
        return false;
    }
    it->position = std::clamp(position, 0.0f, 1.0f);
    std::sort(gradient_.stops.begin(), gradient_.stops.end(),
              [](const GradientStop& a, const GradientStop& b) {
                  return a.position < b.position;
              });
    preview_dirty_ = true;
    Invalidate();
    return true;
}

std::optional<std::uint32_t> GradientEditor::SelectedStopId() const {
    return selected_;
}

void GradientEditor::SelectStop(std::uint32_t id) {
    const bool exists =
        std::any_of(gradient_.stops.begin(), gradient_.stops.end(),
                    [id](const GradientStop& s) { return s.id == id; });
    SelectInternal(exists ? std::optional<std::uint32_t>(id)
                          : std::nullopt);
}

void GradientEditor::SetOnGradientChange(
    std::function<void(const Gradient&)> callback) {
    on_change_ = std::move(callback);
}

void GradientEditor::SetOnGradientCommitted(
    std::function<void(const Gradient&)> callback) {
    on_committed_ = std::move(callback);
}

void GradientEditor::SetOnStopSelected(
    std::function<void(std::uint32_t id)> callback) {
    on_stop_selected_ = std::move(callback);
}

Theme::Color GradientEditor::SampleAt(float t) const {
    return interp_ == Interpolation::Oklch ? SampleOklch(t)
                                           : SampleSrgb(t);
}

Theme::Color GradientEditor::SampleSrgb(float t) const {
    if (gradient_.stops.empty()) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    const auto& stops = gradient_.stops;
    t = std::clamp(t, 0.0f, 1.0f);
    if (t <= stops.front().position) {
        return stops.front().color;
    }
    if (t >= stops.back().position) {
        return stops.back().color;
    }
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].position) {
            const auto& a = stops[i - 1];
            const auto& b = stops[i];
            const float span = b.position - a.position;
            const float u = span > 0.0f ? (t - a.position) / span : 0.0f;
            return Lerp(a.color, b.color, u);
        }
    }
    return stops.back().color;
}

Theme::Color GradientEditor::SampleOklch(float t) const {
    if (gradient_.stops.empty()) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    const auto& stops = gradient_.stops;
    t = std::clamp(t, 0.0f, 1.0f);
    const auto lch_of = [](const GradientStop& s) {
        return CS::OklchFromSrgb(s.color);
    };
    if (t <= stops.front().position) {
        return stops.front().color;
    }
    if (t >= stops.back().position) {
        return stops.back().color;
    }
    for (std::size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].position) {
            const CS::Oklch a = lch_of(stops[i - 1]);
            const CS::Oklch b = lch_of(stops[i]);
            const float span =
                stops[i].position - stops[i - 1].position;
            const float u =
                span > 0.0f ? (t - stops[i - 1].position) / span : 0.0f;
            const CS::Oklch mid{a.l + (b.l - a.l) * u,
                                a.c + (b.c - a.c) * u,
                                LerpHue(a.h, b.h, u),
                                a.a + (b.a - a.a) * u};
            return CS::ClampSrgb(CS::SrgbFromOklch(mid));
        }
    }
    return stops.back().color;
}

// ── Layout ────────────────────────────────────────────────────────────────

GradientEditor::Chrome GradientEditor::GetChrome() const {
    Chrome c{};
    const auto b = GetBounds();
    float y = b.y + kPad;
    const float inner_x = b.x + kPad;
    const float inner_w = std::max(0.0f, b.width - 2.0f * kPad);

    c.tabs = {inner_x, y, inner_w, kTabsHeight};
    y += kTabsHeight + kGap;
    c.preview = {inner_x, y, inner_w, kPreviewHeight};
    y += kPreviewHeight + kGap;
    c.stop_bar = {inner_x + kHandleSize * 0.5f, y,
                  inner_w - kHandleSize, kStopBarHeight};
    y += kStopBarHeight + kGap;
    c.angle_row = {inner_x, y, inner_w, kAngleRowHeight};
    c.dial = {inner_x, y + 2.0f, kDialSize, kDialSize};
    return c;
}

void GradientEditor::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

float GradientEditor::StopBarT(float x) const {
    const Chrome c = GetChrome();
    if (c.stop_bar.width <= 0.0f) {
        return 0.0f;
    }
    return std::clamp((x - c.stop_bar.x) / c.stop_bar.width, 0.0f, 1.0f);
}

Layout::Point GradientEditor::StopHandlePos(const GradientStop& stop) const {
    const Chrome c = GetChrome();
    return {c.stop_bar.x + stop.position * c.stop_bar.width,
            c.stop_bar.y + c.stop_bar.height * 0.5f};
}

std::optional<std::uint32_t> GradientEditor::HitStop(
    const Layout::Point& p) const {
    std::optional<std::uint32_t> best;
    float best_d = kHandleHit;
    for (const auto& s : gradient_.stops) {
        const auto pos = StopHandlePos(s);
        const float dx = p.x - pos.x;
        const float dy = p.y - pos.y;
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d < best_d) {
            best_d = d;
            best = s.id;
        }
    }
    return best;
}

void GradientEditor::ApplyEdit(bool live) {
    preview_dirty_ = true;
    Invalidate();
    if (live && on_change_) {
        on_change_(gradient_);
    }
}

void GradientEditor::Commit() {
    if (on_committed_) {
        on_committed_(gradient_);
    }
}

void GradientEditor::SelectInternal(std::optional<std::uint32_t> id) {
    if (selected_ == id) {
        return;
    }
    selected_ = id;
    Invalidate();
    if (on_stop_selected_) {
        on_stop_selected_(id.value_or(0));
    }
}

std::uint32_t GradientEditor::NextId() {
    if (next_id_ == 0) {
        next_id_ = 1;
    }
    return next_id_++;
}

// ── Preview rasterization ─────────────────────────────────────────────────

void GradientEditor::RasterizePreview(int w, int h,
                                      std::vector<std::uint8_t>* out) const {
    out->resize(static_cast<std::size_t>(w) * h * 4);
    std::uint8_t* px = out->data();
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    // CSS gradient direction: 0deg up, 90deg right.
    const float rad = gradient_.angle_deg * kPi / 180.0f;
    const float dx = std::sin(rad);
    const float dy = -std::cos(rad);
    const float line_len =
        std::max(1.0f, std::fabs(cx * dx) + std::fabs(cy * dy));
    const float max_r = std::max(cx, cy);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float rx = (x + 0.5f) - cx;
            const float ry = (y + 0.5f) - cy;
            float t = 0.0f;
            switch (gradient_.type) {
            case GradientType::Linear:
                t = 0.5f + (rx * dx + ry * dy) / (2.0f * line_len);
                break;
            case GradientType::Radial:
                t = std::sqrt(rx * rx + ry * ry) / max_r;
                break;
            case GradientType::Conic: {
                // CSS conic starts at 0deg (up) and turns clockwise.
                float a = std::atan2(rx, -ry) / (2.0f * kPi);
                t = a < 0.0f ? a + 1.0f : a;
                break;
            }
            }
            const Theme::Color color = SampleAt(t);
            CheckerBgra(color, x, y,
                        px + (static_cast<std::size_t>(y) * w + x) * 4);
        }
    }
}

// ── Rendering ─────────────────────────────────────────────────────────────

void GradientEditor::Render(Rendering::Renderer& renderer,
                            const Theme::Tokens& tokens) {
    const Chrome c = GetChrome();
    const Rendering::TextStyle tab_active{tokens.foreground,
                                          tokens.font_small, 600};
    const Rendering::TextStyle tab_idle{tokens.muted, tokens.font_small,
                                        500};

    // ── Type tabs ──
    renderer.FillRect(c.tabs, tokens.secondary, tokens.radius_medium);
    const float cell_w = c.tabs.width / 3.0f;
    for (int i = 0; i < 3; ++i) {
        const Layout::Rect cell{c.tabs.x + i * cell_w, c.tabs.y, cell_w,
                                c.tabs.height};
        const bool active = static_cast<int>(gradient_.type) == i;
        if (active) {
            renderer.FillRect({cell.x + 2.0f, cell.y + 2.0f,
                               cell.width - 4.0f, cell.height - 4.0f},
                              tokens.surface, tokens.radius_small);
        }
        const auto& style = active ? tab_active : tab_idle;
        const auto size = renderer.MeasureText(kTypeLabels[i], style);
        renderer.DrawText(kTypeLabels[i],
                          {cell.x + (cell.width - size.width) / 2.0f,
                           cell.y + (cell.height - size.height) / 2.0f,
                           size.width + 2.0f, size.height},
                          style);
    }

    // ── Preview band ──
    {
        const int rw = std::clamp(static_cast<int>(c.preview.width), 8,
                                  kPreviewRasterMaxW);
        const int rh = std::clamp(static_cast<int>(c.preview.height), 8,
                                  kPreviewRasterMaxH);
        if (preview_dirty_ || rw != preview_raster_w_ ||
            rh != preview_raster_h_) {
            RasterizePreview(rw, rh, &preview_raster_);
            preview_raster_w_ = rw;
            preview_raster_h_ = rh;
            preview_dirty_ = false;
        }
        renderer.PushClip(c.preview);
        if (!renderer.DrawFrameBgra(preview_raster_.data(),
                                    preview_raster_w_, preview_raster_h_,
                                    c.preview)) {
            for (int i = 0; i < 32; ++i) {
                Theme::Color cc = SampleAt((i + 0.5f) / 32.0f);
                cc.a = 1.0f;
                renderer.FillRect({c.preview.x +
                                       i * c.preview.width / 32.0f,
                                   c.preview.y,
                                   c.preview.width / 32.0f + 1.0f,
                                   c.preview.height},
                                  cc, 0.0f);
            }
        }
        renderer.PopClip();
        renderer.StrokeRect(c.preview, tokens.border, 1.0f,
                            tokens.radius_small);
    }

    // ── Stop bar ──
    {
        const float track_y =
            c.stop_bar.y + (c.stop_bar.height - kTrackHeight) * 0.5f;
        const Layout::Rect track{c.stop_bar.x, track_y, c.stop_bar.width,
                                 kTrackHeight};
        renderer.FillRect(track, tokens.secondary,
                          kTrackHeight * 0.5f);
        // Mini ramp on the track itself so handle positions read against
        // the gradient, not just the preview above.
        renderer.PushClip(track);
        for (int i = 0; i < 48; ++i) {
            Theme::Color cc = SampleAt((i + 0.5f) / 48.0f);
            cc.a = 1.0f;
            renderer.FillRect({track.x + i * track.width / 48.0f, track.y,
                               track.width / 48.0f + 1.0f, track.height},
                              cc, 0.0f);
        }
        renderer.PopClip();
        renderer.StrokeRect(track, tokens.border, 1.0f,
                            kTrackHeight * 0.5f);

        for (const auto& s : gradient_.stops) {
            const bool being_removed =
                drag_ == Drag::Stop && drag_removed_ && s.id == drag_id_;
            if (being_removed) {
                continue;  // ghost: hidden while dragged off the bar
            }
            const auto pos = StopHandlePos(s);
            const Layout::Rect h{pos.x - kHandleSize * 0.5f,
                                 pos.y - kHandleSize * 0.5f, kHandleSize,
                                 kHandleSize};
            const bool sel = selected_ == s.id;
            renderer.FillRect(h, s.color, tokens.radius_small);
            renderer.StrokeRect(
                h, sel ? tokens.accent : tokens.border,
                sel ? 2.0f : 1.0f, tokens.radius_small);
        }
    }

    // ── Angle dial (linear only — dimmed otherwise) ──
    {
        const bool enabled = gradient_.type == GradientType::Linear;
        const Theme::Color dim = enabled ? tokens.foreground
                                         : tokens.disabled;
        const Layout::Point center{c.dial.x + c.dial.width * 0.5f,
                                   c.dial.y + c.dial.height * 0.5f};
        const float r = c.dial.width * 0.5f - 3.0f;
        renderer.FillRect(c.dial, tokens.secondary, c.dial.width * 0.5f);
        renderer.StrokeRect(c.dial,
                            enabled ? tokens.border : tokens.disabled,
                            1.0f, c.dial.width * 0.5f);
        // Direction needle.
        const float rad = gradient_.angle_deg * kPi / 180.0f;
        const Layout::Point tip{center.x + std::sin(rad) * r,
                                center.y - std::cos(rad) * r};
        renderer.DrawLine(center, tip, dim, 1.5f);
        renderer.FillRect({tip.x - 3.0f, tip.y - 3.0f, 6.0f, 6.0f}, dim,
                          3.0f);

        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.0f deg",
                      static_cast<double>(gradient_.angle_deg));
        renderer.DrawText(buf,
                          {c.dial.Right() + kGap, c.angle_row.y + 12.0f,
                           120.0f, 18.0f},
                          Rendering::TextStyle{dim, tokens.font_small, 500});
    }

    if (IsFocused()) {
        RenderFocusRing(renderer, tokens, tokens.radius_medium);
    }
}

// ── Events ────────────────────────────────────────────────────────────────

bool GradientEditor::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    const Chrome c = GetChrome();

    switch (event.type) {
    case Event::Type::MouseDown:
        if (event.mouse_button != MouseButton::Left) {
            return false;
        }
        // Type tabs.
        if (c.tabs.Contains(event.position)) {
            const int idx = std::clamp(
                static_cast<int>((event.position.x - c.tabs.x) /
                                 (c.tabs.width / 3.0f)),
                0, 2);
            SetType(static_cast<GradientType>(idx));
            return true;
        }
        // Angle dial (linear only).
        if (gradient_.type == GradientType::Linear &&
            c.dial.Contains(event.position)) {
            drag_ = Drag::Dial;
            const Layout::Point center{c.dial.x + c.dial.width * 0.5f,
                                       c.dial.y + c.dial.height * 0.5f};
            const float a = std::atan2(event.position.x - center.x,
                                       center.y - event.position.y);
            SetAngle(a * 180.0f / kPi);
            ApplyEdit(true);
            return true;
        }
        // Existing stop?
        if (const auto hit = HitStop(event.position)) {
            drag_ = Drag::Stop;
            drag_id_ = *hit;
            drag_removed_ = false;
            SelectInternal(drag_id_);
            return true;
        }
        // Click on the stop track adds a stop at that position.
        if (c.stop_bar.Contains(event.position)) {
            const float t = StopBarT(event.position.x);
            const Theme::Color color = SampleAt(t);
            AddStop(color, t);
            drag_ = Drag::Stop;
            drag_id_ = *selected_;
            drag_removed_ = false;
            ApplyEdit(true);
            return true;
        }
        return false;

    case Event::Type::MouseMove:
        if (drag_ == Drag::Stop) {
            const float t = StopBarT(event.position.x);
            // Drag-out removal: far enough off the track marks the stop
            // for deletion on release; coming back cancels it.
            const float dy =
                std::fabs(event.position.y -
                          (c.stop_bar.y + c.stop_bar.height * 0.5f));
            drag_removed_ = dy > kDragOutDistance;
            if (!drag_removed_) {
                SetStopPosition(drag_id_, t);
            }
            ApplyEdit(true);
            return true;
        }
        if (drag_ == Drag::Dial) {
            const Layout::Point center{c.dial.x + c.dial.width * 0.5f,
                                       c.dial.y + c.dial.height * 0.5f};
            const float a = std::atan2(event.position.x - center.x,
                                       center.y - event.position.y);
            SetAngle(a * 180.0f / kPi);
            ApplyEdit(true);
            return true;
        }
        return false;

    case Event::Type::MouseUp:
        if (event.mouse_button == MouseButton::Left &&
            drag_ != Drag::None) {
            if (drag_ == Drag::Stop && drag_removed_) {
                RemoveStop(drag_id_);
            }
            drag_ = Drag::None;
            drag_id_ = 0;
            drag_removed_ = false;
            Commit();
            return true;
        }
        return false;

    case Event::Type::KeyDown:
        if (event.key == KeyCode::Delete && selected_.has_value()) {
            if (RemoveStop(*selected_)) {
                ApplyEdit(true);
                Commit();
                return true;
            }
        }
        // Arrow keys nudge the selected stop.
        if ((event.key == KeyCode::ArrowLeft ||
             event.key == KeyCode::ArrowRight) &&
            selected_.has_value()) {
            const auto it = std::find_if(
                gradient_.stops.begin(), gradient_.stops.end(),
                [this](const GradientStop& s) {
                    return s.id == *selected_;
                });
            if (it != gradient_.stops.end()) {
                const float step =
                    event.key == KeyCode::ArrowLeft ? -0.01f : 0.01f;
                SetStopPosition(*selected_, it->position + step);
                ApplyEdit(true);
                Commit();
                return true;
            }
        }
        return false;

    case Event::Type::Blur:
        drag_ = Drag::None;
        drag_id_ = 0;
        drag_removed_ = false;
        return false;

    default:
        return false;
    }
}

Layout::Size GradientEditor::GetMinSize() const {
    return {220.0f, 170.0f};
}

Layout::Size GradientEditor::GetPreferredSize() const {
    return {320.0f,
            2.0f * kPad + kTabsHeight + kPreviewHeight + kStopBarHeight +
                kAngleRowHeight + 3.0f * kGap};
}

Layout::Size GradientEditor::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
