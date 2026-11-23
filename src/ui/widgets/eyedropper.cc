// See eyedropper.h — in-canvas sample loupe over a host-provided source.
#include "eyedropper.h"

#include <algorithm>
#include <utility>

#include "colorspace/oklch.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kLoupeGap = 14.0f;     // gap between cursor and loupe
constexpr float kReadoutHeight = 18.0f;

Theme::Color WithAlpha(Theme::Color c, float a) {
    c.a *= a;
    return c;
}

} // namespace

Eyedropper::Eyedropper() {
    SetFocusable(true);
}

void Eyedropper::SetSampleSource(SampleFn fn) {
    sample_ = std::move(fn);
}

void Eyedropper::SetSampleRadius(int radius_px) {
    sample_radius_ = std::clamp(radius_px, 0, 8);
}

void Eyedropper::SetMagnifier(int cells, int cell_px) {
    mag_cells_ = std::clamp(cells | 1, 3, 31);  // force odd for a center cell
    mag_cell_px_ = std::clamp(cell_px, 4, 24);
    Invalidate();
}

void Eyedropper::SetActive(bool active) {
    if (active_ == active) {
        return;
    }
    active_ = active;
    if (!active_) {
        cursor_valid_ = false;
    }
    Invalidate();
}

void Eyedropper::SetOnPick(std::function<void(Theme::Color)> callback) {
    on_pick_ = std::move(callback);
}

void Eyedropper::SetOnCancel(std::function<void()> callback) {
    on_cancel_ = std::move(callback);
}

void Eyedropper::SetOnHoverSample(
    std::function<void(Theme::Color)> callback) {
    on_hover_sample_ = std::move(callback);
}

Theme::Color Eyedropper::SampleAt(const Layout::Point& p) const {
    if (!sample_) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    if (sample_radius_ <= 0) {
        return sample_(p.x, p.y);
    }
    const int r = sample_radius_;
    float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    int n = 0;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const Theme::Color c =
                sample_(p.x + dx, p.y + dy);
            acc[0] += c.r;
            acc[1] += c.g;
            acc[2] += c.b;
            acc[3] += c.a;
            ++n;
        }
    }
    const float inv = n > 0 ? 1.0f / n : 0.0f;
    return {acc[0] * inv, acc[1] * inv, acc[2] * inv, acc[3] * inv};
}

Theme::Color Eyedropper::CurrentSample() const {
    if (!active_ || !cursor_valid_ || !sample_) {
        return {0.0f, 0.0f, 0.0f, 0.0f};
    }
    return SampleAt(cursor_);
}

Layout::Rect Eyedropper::LoupeRect() const {
    const float size =
        static_cast<float>(mag_cells_ * mag_cell_px_) + 8.0f;
    const float height = size + (show_hex_ ? kReadoutHeight + 4.0f : 0.0f);
    const auto b = GetBounds();
    // Prefer bottom-right of the cursor; flip to keep the loupe on-screen.
    float x = cursor_.x + kLoupeGap + 12.0f;
    float y = cursor_.y + kLoupeGap + 12.0f;
    if (x + size > b.Right() - 4.0f) {
        x = cursor_.x - kLoupeGap - 12.0f - size;
    }
    if (y + height > b.Bottom() - 4.0f) {
        y = cursor_.y - kLoupeGap - 12.0f - height;
    }
    x = std::clamp(x, b.x + 4.0f, b.Right() - size - 4.0f);
    y = std::clamp(y, b.y + 4.0f, b.Bottom() - height - 4.0f);
    return {x, y, size, height};
}

void Eyedropper::Render(Rendering::Renderer& renderer,
                        const Theme::Tokens& tokens) {
    if (!active_) {
        return;
    }
    const auto b = GetBounds();
    // Quiet scrim cue so the mode is legible over the canvas.
    renderer.FillRect(b, WithAlpha(tokens.background, 0.18f), 0.0f);

    if (!cursor_valid_) {
        renderer.DrawText(
            sample_ ? "click to sample · esc to cancel"
                    : "no sample source · esc to cancel",
            {b.x + 12.0f, b.y + 12.0f, 320.0f, 18.0f},
            Rendering::TextStyle{tokens.foreground, tokens.font_small, 500});
        return;
    }

    // Crosshair at the sample point.
    renderer.DrawLine({cursor_.x - 7.0f, cursor_.y},
                      {cursor_.x + 7.0f, cursor_.y},
                      WithAlpha(tokens.foreground, 0.85f), 1.0f);
    renderer.DrawLine({cursor_.x, cursor_.y - 7.0f},
                      {cursor_.x, cursor_.y + 7.0f},
                      WithAlpha(tokens.foreground, 0.85f), 1.0f);

    const Layout::Rect loupe = LoupeRect();
    renderer.FillRect(loupe, WithAlpha(tokens.surface, 0.97f),
                      tokens.radius_medium);
    renderer.StrokeRect(loupe, tokens.border, 1.0f, tokens.radius_medium);

    const float grid = static_cast<float>(mag_cells_ * mag_cell_px_);
    const Layout::Rect grid_rect{loupe.x + 4.0f, loupe.y + 4.0f, grid,
                                 grid};
    const int half = mag_cells_ / 2;
    for (int gy = -half; gy <= half; ++gy) {
        for (int gx = -half; gx <= half; ++gx) {
            Theme::Color c{0.0f, 0.0f, 0.0f, 0.0f};
            if (sample_) {
                c = sample_(cursor_.x + gx, cursor_.y + gy);
            }
            const Layout::Rect cell{
                grid_rect.x + (gx + half) * mag_cell_px_,
                grid_rect.y + (gy + half) * mag_cell_px_,
                static_cast<float>(mag_cell_px_),
                static_cast<float>(mag_cell_px_)};
            // Sampled cells paint their color; out-of-source samples
            // (transparent) get the muted checker treatment.
            if (c.a <= 0.0f) {
                renderer.FillRect(cell, tokens.secondary, 0.0f);
            } else {
                renderer.FillRect(cell, c, 0.0f);
            }
        }
    }
    // Center-cell highlight = exactly what a click picks.
    const Layout::Rect center_cell{
        grid_rect.x + half * mag_cell_px_,
        grid_rect.y + half * mag_cell_px_,
        static_cast<float>(mag_cell_px_),
        static_cast<float>(mag_cell_px_)};
    renderer.StrokeRect({center_cell.x - 1.0f, center_cell.y - 1.0f,
                         center_cell.width + 2.0f,
                         center_cell.height + 2.0f},
                        tokens.accent, 1.5f, 0.0f);
    renderer.StrokeRect(grid_rect, tokens.border, 1.0f, 0.0f);

    if (show_hex_) {
        const Theme::Color c = SampleAt(cursor_);
        const std::string label = sample_
            ? ColorSpace::HexFromColor(c, c.a < 1.0f)
            : std::string("no source");
        const Layout::Rect rect{loupe.x + 4.0f,
                                grid_rect.Bottom() + 4.0f,
                                loupe.width - 8.0f, kReadoutHeight};
        renderer.DrawText(label, rect,
                          Rendering::TextStyle{tokens.foreground,
                                               tokens.font_small, 600,
                                               true});
    }
}

void Eyedropper::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Eyedropper::HandleEvent(const Event& event) {
    if (!active_ || !IsEnabled()) {
        return false;
    }
    switch (event.type) {
    case Event::Type::MouseMove:
        cursor_ = event.position;
        cursor_valid_ = true;
        Invalidate();
        if (on_hover_sample_ && sample_) {
            on_hover_sample_(SampleAt(cursor_));
        }
        return true;

    case Event::Type::MouseDown:
        if (event.mouse_button == MouseButton::Left) {
            cursor_ = event.position;
            cursor_valid_ = true;
            if (sample_) {
                const Theme::Color c = SampleAt(cursor_);
                if (on_pick_) {
                    on_pick_(c);
                }
            }
            SetActive(false);
            return true;
        }
        if (event.mouse_button == MouseButton::Right) {
            // Right click cancels — picking under a loupe should never
            // force a color write.
            SetActive(false);
            if (on_cancel_) {
                on_cancel_();
            }
            return true;
        }
        return false;

    case Event::Type::Click:
        // Consume so the click that ended the pick doesn't fall through
        // to whatever sits under the overlay.
        return true;

    case Event::Type::KeyDown:
        if (event.key == KeyCode::Escape) {
            SetActive(false);
            if (on_cancel_) {
                on_cancel_();
            }
            return true;
        }
        return false;

    case Event::Type::Blur:
        cursor_valid_ = false;
        return false;

    default:
        // While active, eat all events — the overlay owns the surface.
        return true;
    }
}

Layout::Size Eyedropper::GetMinSize() const {
    return {64.0f, 64.0f};
}

Layout::Size Eyedropper::GetPreferredSize() const {
    // Overlay: fill whatever it is laid over.
    return {100000.0f, 100000.0f};
}

Layout::Size Eyedropper::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
