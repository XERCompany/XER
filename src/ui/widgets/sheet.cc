#include "sheet.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Sheet> Sheet::Create(std::string title,
                                     std::shared_ptr<Widget> content,
                                     Side side) {
    return std::shared_ptr<Sheet>(
        new Sheet(std::move(title), std::move(content), side));
}

Sheet::Sheet(std::string title, std::shared_ptr<Widget> content, Side side)
    : title_(std::move(title)), content_(std::move(content)), side_(side) {
    SetFocusable(false);
    // Slide-in from the anchored edge.
    open_t_.Set(1.0f, 0.22f);
}

void Sheet::SetTitle(std::string title) {
    title_ = std::move(title);
    Invalidate();
}

void Sheet::SetContent(std::shared_ptr<Widget> content) {
    content_ = std::move(content);
    if (content_) AddChild(content_);
    RequestLayout();
    Invalidate();
}

void Sheet::SetSide(Side side) {
    side_ = side;
    RequestLayout();
    Invalidate();
}

void Sheet::SetPanelSize(float size) {
    panel_size_ = std::max(160.0f, size);
    RequestLayout();
}

void Sheet::SetOnClosed(std::function<void()> callback) {
    on_closed_ = std::move(callback);
}

void Sheet::Close() {
    if (closed_) return;
    closed_ = true;
    dismissed_ = true;
    if (on_closed_) on_closed_();
    if (GetTree() != nullptr) HideOverlay();
}

Layout::Rect Sheet::PanelRect(const Layout::Rect& bounds) const {
    Layout::Rect r;
    r.width = bounds.width;
    r.height = bounds.height;
    switch (side_) {
        case Side::Left:
            r.width = std::min(panel_size_, bounds.width * 0.9f);
            r.x = bounds.x;
            r.y = bounds.y;
            break;
        case Side::Right:
            r.width = std::min(panel_size_, bounds.width * 0.9f);
            r.x = bounds.x + bounds.width - r.width;
            r.y = bounds.y;
            break;
        case Side::Top:
            r.height = std::min(panel_size_, bounds.height * 0.9f);
            r.x = bounds.x;
            r.y = bounds.y;
            break;
        case Side::Bottom:
        default:
            r.height = std::min(panel_size_, bounds.height * 0.9f);
            r.x = bounds.x;
            r.y = bounds.y + bounds.height - r.height;
            break;
    }
    return r;
}

void Sheet::Render(Rendering::Renderer& renderer,
                   const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    if (bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    const float t = open_t_.Value();
    const float ease = 1.0f - (1.0f - t) * (1.0f - t);  // ease-out quad

    // Scrim: translucent dark over the whole client area.
    Theme::Color scrim = tokens.background;
    scrim.a = scrim.a * 0.55f * ease;
    renderer.FillRect(bounds, scrim, 0.0f);

    const auto panel = PanelRect(bounds);
    // Slide offset along the anchored axis during entrance.
    Layout::Rect p = panel;
    const float slide = (1.0f - ease) * panel.width;
    switch (side_) {
        case Side::Left:  p.x -= slide; break;
        case Side::Right: p.x += slide; break;
        case Side::Top:   p.y -= slide; break;
        case Side::Bottom:p.y += slide; break;
    }

    renderer.FillRect(p, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(p, tokens.border, 1.0f, tokens.radius_large);

    // Header: title + close ×.
    const float pad = 16.0f;
    close_rect_ = {p.x + p.width - 36.0f, p.y + 10.0f, 26.0f, 26.0f};
    if (!title_.empty()) {
        const Rendering::TextStyle style{tokens.foreground,
                                         tokens.font_title, 600};
        Layout::Rect tr{p.x + pad, p.y + 12.0f, p.width - pad * 2.0f - 40.0f,
                        tokens.font_title * 1.4f};
        renderer.DrawText(title_, tr, style);
    }
    // Close glyph.
    const float cx = close_rect_.x + close_rect_.width * 0.5f;
    const float cy = close_rect_.y + close_rect_.height * 0.5f;
    const float arm = 5.0f;
    renderer.DrawLine({cx - arm, cy - arm}, {cx + arm, cy + arm},
                      tokens.foreground, 1.4f);
    renderer.DrawLine({cx + arm, cy - arm}, {cx - arm, cy + arm},
                      tokens.foreground, 1.4f);

    // Content is a real child — RenderSubtree draws it; Layout() assigns
    // its rect below the header.
    rendered_ = true;
}

void Sheet::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    if (content_) {
        const auto panel = PanelRect(bounds);
        const float pad = 16.0f;
        const float header_h = title_.empty() ? 24.0f : 52.0f;
        Layout::Rect cr{panel.x + pad, panel.y + header_h,
                        panel.width - pad * 2.0f,
                        std::max(0.0f, panel.height - header_h - pad)};
        content_->Layout(cr, CachedLayoutTokens());
    }
}

void Sheet::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) return;
    if (!ticked_ && rendered_) {
        open_t_.Set(1.0f, 0.0f);
    }
    ticked_ = true;
    open_t_.Update(static_cast<float>(dt_seconds));
    if (open_t_.Active()) Invalidate();
}

bool Sheet::HandleEvent(const Event& event) {
    if (closed_) return true;
    switch (event.type) {
        case Event::Type::Click: {
            const auto& p = event.position;
            const auto panel = PanelRect(GetBounds());
            const bool inside =
                p.x >= panel.x && p.x < panel.x + panel.width &&
                p.y >= panel.y && p.y < panel.y + panel.height;
            if (!inside) {
                Close();  // scrim click dismisses
                return true;
            }
            if (close_rect_.width > 0.0f &&
                p.x >= close_rect_.x && p.x < close_rect_.x + close_rect_.width &&
                p.y >= close_rect_.y && p.y < close_rect_.y + close_rect_.height) {
                Close();
                return true;
            }
            return true;  // modal: consume panel clicks
        }
        case Event::Type::KeyDown:
            if (event.key == KeyCode::Escape) {
                Close();
                return true;
            }
            return true;
        default:
            return true;  // modal: consume everything else too
    }
}

Layout::Size Sheet::GetMinSize() const {
    return {0.0f, 0.0f};
}

Layout::Size Sheet::GetPreferredSize() const {
    return {panel_size_, panel_size_};
}

Layout::Size Sheet::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
