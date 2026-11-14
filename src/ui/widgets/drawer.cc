#include "drawer.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Drawer> Drawer::Create(std::string title,
                                       std::shared_ptr<Widget> content) {
    return std::shared_ptr<Drawer>(new Drawer(std::move(title),
                                            std::move(content)));
}

Drawer::Drawer(std::string title, std::shared_ptr<Widget> content)
    : title_(std::move(title)), content_(std::move(content)) {
    SetFocusable(false);
    open_t_.Set(1.0f, 0.24f);
}

void Drawer::SetTitle(std::string title) {
    title_ = std::move(title);
    Invalidate();
}

void Drawer::SetContent(std::shared_ptr<Widget> content) {
    content_ = std::move(content);
    if (content_) AddChild(content_);
    RequestLayout();
    Invalidate();
}

void Drawer::SetSnapPoint(float fraction) {
    snap_fraction_ = std::clamp(fraction, 0.15f, 0.95f);
    RequestLayout();
}

void Drawer::SetOnClosed(std::function<void()> callback) {
    on_closed_ = std::move(callback);
}

void Drawer::Close() {
    if (closed_) return;
    closed_ = true;
    dismissed_ = true;
    if (on_closed_) on_closed_();
    if (GetTree() != nullptr) HideOverlay();
}

Layout::Rect Drawer::PanelRect(const Layout::Rect& bounds) const {
    Layout::Rect r;
    r.x = bounds.x;
    r.width = bounds.width;
    const float h = bounds.height * snap_fraction_ + drag_offset_;
    r.height = std::clamp(h, bounds.height * 0.15f, bounds.height * 0.95f);
    r.y = bounds.y + bounds.height - r.height;
    return r;
}

void Drawer::Render(Rendering::Renderer& renderer,
                    const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    const float t = open_t_.Value();
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);

    Theme::Color scrim = tokens.background;
    scrim.a = scrim.a * 0.55f * ease;
    renderer.FillRect(bounds, scrim, 0.0f);

    const auto panel = PanelRect(bounds);
    Layout::Rect p = panel;
    p.y += (1.0f - ease) * panel.height;  // slide up from bottom

    // Rounded top corners only (approximated with a full rounded rect —
    // the bottom edge sits flush off-panel anyway at full height).
    renderer.FillRect(p, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(p, tokens.border, 1.0f, tokens.radius_large);

    // Drag handle pill at the top center.
    handle_rect_ = {p.x + p.width * 0.5f - 24.0f, p.y + 8.0f, 48.0f, 5.0f};
    renderer.FillRect(handle_rect_, tokens.muted, 2.5f);

    const float pad = 16.0f;
    close_rect_ = {p.x + p.width - 36.0f, p.y + 8.0f, 26.0f, 26.0f};
    if (!title_.empty()) {
        const Rendering::TextStyle style{tokens.foreground,
                                         tokens.font_title, 600};
        Layout::Rect tr{p.x + pad, p.y + 22.0f, p.width - pad * 2.0f - 40.0f,
                        tokens.font_title * 1.4f};
        renderer.DrawText(title_, tr, style);
    }
    const float cx = close_rect_.x + close_rect_.width * 0.5f;
    const float cy = close_rect_.y + close_rect_.height * 0.5f;
    const float arm = 5.0f;
    renderer.DrawLine({cx - arm, cy - arm}, {cx + arm, cy + arm},
                      tokens.foreground, 1.4f);
    renderer.DrawLine({cx + arm, cy - arm}, {cx - arm, cy + arm},
                      tokens.foreground, 1.4f);

    rendered_ = true;
}

void Drawer::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (content_) {
        const auto panel = PanelRect(bounds);
        const float pad = 16.0f;
        const float header_h = title_.empty() ? 40.0f : 62.0f;
        Layout::Rect cr{panel.x + pad, panel.y + header_h,
                        panel.width - pad * 2.0f,
                        std::max(0.0f, panel.height - header_h - pad)};
        content_->Layout(cr, CachedLayoutTokens());
    }
}

void Drawer::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) return;
    if (!ticked_ && rendered_) open_t_.Set(1.0f, 0.0f);
    ticked_ = true;
    open_t_.Update(static_cast<float>(dt_seconds));
    if (open_t_.Active() || dragging_) Invalidate();
}

bool Drawer::HandleEvent(const Event& event) {
    if (closed_) return true;
    switch (event.type) {
        case Event::Type::MouseDown:
            if (handle_rect_.width > 0.0f) {
                const auto& p = event.position;
                if (p.x >= handle_rect_.x &&
                    p.x < handle_rect_.x + handle_rect_.width &&
                    p.y >= handle_rect_.y &&
                    p.y < handle_rect_.y + handle_rect_.height) {
                    dragging_ = true;
                    drag_offset_ = 0.0f;
                    last_drag_y_ = p.y;
                    return true;
                }
            }
            return true;
        case Event::Type::MouseMove:
            if (dragging_) {
                // Positive dy = drag down = smaller panel.
                drag_offset_ += event.position.y - last_drag_y_;
                last_drag_y_ = event.position.y;
                drag_offset_ = std::clamp(drag_offset_, -80.0f, 200.0f);
                Invalidate();
            }
            return true;
        case Event::Type::MouseUp:
            if (dragging_) {
                dragging_ = false;
                // Snap: dragged mostly down → dismiss.
                if (drag_offset_ > 80.0f) {
                    Close();
                } else {
                    drag_offset_ = 0.0f;
                }
                Invalidate();
            }
            return true;
        case Event::Type::Click: {
            const auto& p = event.position;
            const auto panel = PanelRect(GetBounds());
            const bool inside =
                p.x >= panel.x && p.x < panel.x + panel.width &&
                p.y >= panel.y && p.y < panel.y + panel.height;
            if (!inside) {
                Close();
                return true;
            }
            if (close_rect_.width > 0.0f &&
                p.x >= close_rect_.x && p.x < close_rect_.x + close_rect_.width &&
                p.y >= close_rect_.y && p.y < close_rect_.y + close_rect_.height) {
                Close();
            }
            return true;
        }
        case Event::Type::KeyDown:
            if (event.key == KeyCode::Escape) {
                Close();
                return true;
            }
            return true;
        default:
            return true;
    }
}

Layout::Size Drawer::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Drawer::GetPreferredSize() const {
    return {480.0f, 320.0f};
}

Layout::Size Drawer::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
