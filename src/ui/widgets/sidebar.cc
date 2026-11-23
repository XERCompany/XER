#include "sidebar.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {

std::shared_ptr<Sidebar> Sidebar::Create(std::string header) {
    return std::shared_ptr<Sidebar>(new Sidebar(std::move(header)));
}

Sidebar::Sidebar(std::string header) : header_(std::move(header)) {
    collapse_t_.Set(0.0f, 0.18f);
}

void Sidebar::SetHeader(std::string header) {
    header_ = std::move(header);
    Invalidate();
}

void Sidebar::SetItems(std::vector<Item> items) {
    items_ = std::move(items);
    if (active_ >= static_cast<int>(items_.size())) active_ = -1;
    RequestLayout();
    Invalidate();
}

void Sidebar::SetActiveIndex(int index) {
    active_ = (index >= -1 && index < static_cast<int>(items_.size()))
                  ? index
                  : -1;
    Invalidate();
}

namespace {
// Host-installed icon lookup (same pattern as Button's/Tabs's
// resolver). Empty => items always draw their text glyph.
Sidebar::IconResolver g_sidebar_icon_resolver;
} // namespace

void Sidebar::SetIconResolver(IconResolver resolver) {
    g_sidebar_icon_resolver = std::move(resolver);
}

void Sidebar::SetCollapsed(bool collapsed, bool animate) {
    collapsed_ = collapsed;
    if (animate) {
        collapse_t_.Set(collapsed ? 1.0f : 0.0f, 0.18f);
    } else {
        // Snap: land on the end state now so the first render is
        // already collapsed (no expanded-label flash).
        collapse_ = collapsed ? 1.0f : 0.0f;
        collapse_t_.from = collapse_t_.to = collapse_;
        collapse_t_.t = 1.0f;
    }
    RequestLayout();
}

void Sidebar::SetOnSelect(std::function<void(int)> cb) {
    on_select_ = std::move(cb);
}

float Sidebar::ItemHeight() const { return 34.0f; }

int Sidebar::ItemAtY(float y, const Layout::Rect& b) const {
    const float top = b.y + (header_.empty() ? 8.0f : 52.0f);
    const float ih = ItemHeight();
    const int idx = static_cast<int>(std::floor((y - top) / ih));
    return (idx >= 0 && idx < static_cast<int>(items_.size())) ? idx : -1;
}

void Sidebar::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

void Sidebar::Render(Rendering::Renderer& renderer,
                     const Theme::Tokens& tokens) {
    const auto b = GetBounds();
    if (b.width <= 0.0f || b.height <= 0.0f) return;

    // Two chrome modes: a floating card (rounded fill + ring) when the
    // sidebar owns a wide column, or a docked edge rail (flat fill +
    // one hairline seam) when it is narrow — the rail look NLE shells
    // use for the far-left icon strip.
    if (b.width < 72.0f) {
        renderer.FillRect(b, tokens.surface, 0.0f);
        renderer.DrawLine({b.x + b.width - 0.5f, b.y},
                          {b.x + b.width - 0.5f, b.y + b.height},
                          tokens.border, 1.0f);
    } else {
        renderer.FillRect(b, tokens.surface, tokens.radius_medium);
        renderer.StrokeRect(b, tokens.border, 1.0f,
                            tokens.radius_medium);
    }

    const bool icon_only = collapse_ > 0.5f;
    // Collapsed item strip: interpolates toward a real icon-rail width
    // (was a 45%-of-bounds shrink that left icons off-center in a
    // sliver whenever the host kept fixed bounds).
    const float collapsed_w = std::min(b.width, 48.0f);
    const float w =
        b.width - collapse_ * (b.width - collapsed_w);

    // Header: section label, not a page title — font_title (30px) does not
    // fit the 26px band and was being clipped.
    if (!header_.empty() && !icon_only) {
        const Rendering::TextStyle hs{tokens.muted,
                                      tokens.font_small, 700};
        const float hh = renderer.MeasureText(header_, hs).height;
        Layout::Rect hr{b.x + 14.0f,
                        b.y + 10.0f + std::max(0.0f, (26.0f - hh) * 0.5f),
                        w - 28.0f, hh + 1.0f};
        renderer.DrawText(header_, hr, hs);
    }

    // Items.
    const float ih = ItemHeight();
    float y = b.y + (header_.empty() ? 8.0f : 52.0f);
    for (size_t i = 0; i < items_.size(); ++i) {
        const auto& item = items_[i];
        const bool act = static_cast<int>(i) == active_;
        const bool hov = static_cast<int>(i) == hovered_ && item.enabled;

        if (item.separator_before) {
            renderer.DrawLine({b.x + 10.0f, y + ih * 0.5f},
                              {b.x + w - 10.0f, y + ih * 0.5f},
                              tokens.border, 1.0f);
        }
        if (act) {
            renderer.FillRect({b.x + 6.0f, y + 3.0f, w - 12.0f, ih - 6.0f},
                              tokens.accent, tokens.radius_small);
        } else if (hov) {
            renderer.FillRect({b.x + 6.0f, y + 3.0f, w - 12.0f, ih - 6.0f},
                              tokens.hover, tokens.radius_small);
        }

        const Theme::Color tc =
            !item.enabled ? tokens.muted
                          : (act ? tokens.accent_foreground
                                 : tokens.foreground);
        const Rendering::TextStyle item_style{tc, tokens.font_small, 500};

        // Icon slot — a resolved bitmap icon wins over the text glyph.
        const auto draw_icon = [&](const Layout::Rect& box) {
            if (!item.icon_name.empty() && g_sidebar_icon_resolver) {
                if (g_sidebar_icon_resolver(
                        renderer, item.icon_name.c_str(), 18, tc,
                        box)) {
                    return;
                }
            }
            if (item.icon.empty()) return;
            const Rendering::TextStyle icon_s{tc, tokens.font_small,
                                              600};
            const auto ts = renderer.MeasureText(item.icon, icon_s);
            // Center the glyph inside the slot rather than drawing from
            // the slot's left edge — 1-2 char rail glyphs read centered.
            const Layout::Rect ir{
                box.x + std::max(0.0f, (box.width - ts.width) * 0.5f),
                box.y, std::min(box.width, ts.width), box.height};
            renderer.DrawText(item.icon, ir, icon_s);
        };
        float tx = b.x + 14.0f;
        if (!item.icon.empty() || !item.icon_name.empty()) {
            draw_icon({b.x + 12.0f, y + ih * 0.5f - 9.0f, 20.0f, 18.0f});
            tx = b.x + 36.0f;
        }
        if (!icon_only) {
            Layout::Rect tr{tx, y + ih * 0.5f - 9.0f,
                            w - (tx - b.x) - 12.0f, 18.0f};
            renderer.DrawText(item.label, tr, item_style);
        } else {
            // Center icon when collapsed.
            draw_icon({b.x + w * 0.5f - 10.0f, y + ih * 0.5f - 9.0f,
                       20.0f, 18.0f});
        }
        y += ih;
        if (y > b.y + b.height) break;
    }
}

void Sidebar::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) return;
    collapse_t_.Update(static_cast<float>(dt_seconds));
    collapse_ = collapse_t_.Value();
    Invalidate();
}

bool Sidebar::HandleEvent(const Event& event) {
    const auto& p = event.position;
    const auto b = GetBounds();

    if (event.type == Event::Type::MouseMove) {
        const int idx = ItemAtY(p.y, b);
        if (idx != hovered_) {
            hovered_ = idx;
            Invalidate();
        }
        return false;
    }
    if (event.type == Event::Type::Click) {
        const int idx = ItemAtY(p.y, b);
        if (idx >= 0 && items_[idx].enabled) {
            active_ = idx;
            if (on_select_) on_select_(idx);
            Invalidate();
            return true;
        }
        return false;
    }
    return false;
}

Layout::Size Sidebar::GetMinSize() const {
    return {48.0f, 120.0f};
}

Layout::Size Sidebar::GetPreferredSize() const {
    return {220.0f, 480.0f};
}

Layout::Size Sidebar::GetMaxSize() const {
    return {320.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
