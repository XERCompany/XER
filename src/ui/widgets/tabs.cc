#include "tabs.h"

#include <algorithm>
#include <utility>

#include "motion/motion.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

std::size_t Utf8Length(std::string_view text) {
    std::size_t count = 0;
    for (const unsigned char byte : text) {
        if ((byte & 0xC0u) != 0x80u) {
            ++count;
        }
    }
    return count;
}

// Per-tab cell width: text estimate + horizontal padding.
float TabWidth(const std::string& label) {
    return static_cast<float>(Utf8Length(label)) * 7.4f + 30.0f;
}

} // namespace

Tabs::Tabs() {
    // Focusable so arrow keys can switch tabs (roving tab semantics).
    SetFocusable(true);
}

void Tabs::AddTab(std::string label, std::string tooltip) {
    labels_.push_back(std::move(label));
    tooltips_.push_back(std::move(tooltip));
    if (active_index_ < 0) {
        active_index_ = 0;
    }
    RequestLayout();
    Invalidate();
}

void Tabs::SetTabTooltip(int index, std::string tooltip) {
    if (index < 0 || index >= static_cast<int>(labels_.size())) {
        return;
    }
    if (tooltips_.size() < labels_.size()) {
        tooltips_.resize(labels_.size());
    }
    tooltips_[static_cast<std::size_t>(index)] = std::move(tooltip);
    if (index == hovered_tab_) {
        SyncTabTooltip();
    }
}

void Tabs::ClearTabs() {
    labels_.clear();
    tooltips_.clear();
    tab_rects_.clear();
    active_index_ = -1;
    hovered_tab_ = -1;
    pressed_tab_ = -1;
    RequestLayout();
    Invalidate();
}

void Tabs::SetActiveIndex(int index) {
    if (index < 0 || index >= static_cast<int>(labels_.size()) || index == active_index_) {
        return;
    }
    active_index_ = index;
    RetargetIndicator();
    Invalidate();
}

void Tabs::SetOnChanged(std::function<void(int)> callback) {
    on_changed_ = std::move(callback);
}

void Tabs::SetStyle(Style style) {
    if (style_ == style) return;
    style_ = style;
    Invalidate();
}

void Tabs::SyncTabTooltip() {
    std::string tip;
    if (hovered_tab_ >= 0 &&
        hovered_tab_ < static_cast<int>(tooltips_.size())) {
        tip = tooltips_[static_cast<std::size_t>(hovered_tab_)];
    }
    // Clear first so the tree drops a tooltip still showing the
    // previous tab's text; Tick() re-arms with the new text before
    // the tree's own tooltip pass re-reads GetTooltip().
    SetTooltip("");
    pending_tooltip_ = std::move(tip);
    tooltip_dirty_ = true;
}

void Tabs::ActivateTab(int index) {
    if (index < 0 || index >= static_cast<int>(labels_.size())) return;
    if (index == active_index_) return;
    SetActiveIndex(index);
    if (on_changed_) {
        on_changed_(index);
    }
}

Layout::Rect Tabs::PillRect(float x, float w, const Layout::Rect& bounds) const {
    // Segmented look: chip inset 5px vertically, 3px horizontally, fully
    // rounded ends (radius = half height).
    constexpr float kInsetX = 3.0f;
    constexpr float kInsetY = 5.0f;
    const float h = std::max(0.0f, bounds.height - 2.0f * kInsetY);
    return Layout::Rect{x + kInsetX, bounds.y + kInsetY,
                        std::max(0.0f, w - 2.0f * kInsetX), h};
}

void Tabs::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.background, 0.0f);

    const bool pill = style_ == Style::Pill;
    const float press = press_t_.Value();

    // Active-tab surface slab first (Underline style only — the Pill
    // style already draws its own chip), then hover washes, then the
    // indicator on top.
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const auto& rect = tab_rects_[i];
        const bool active = static_cast<int>(i) == active_index_;
        const bool hovered = static_cast<int>(i) == hovered_tab_;
        if (!pill && active) {
            const Layout::Rect slab{rect.x + 2.0f, bounds.y + 3.0f,
                                    rect.width - 4.0f,
                                    bounds.height - 4.0f};
            renderer.FillRect(slab, tokens.surface, tokens.radius_medium);
        }
        if (!active && hovered && IsEnabled()) {
            const auto wash = pill
                ? PillRect(rect.x, rect.width, bounds)
                : Layout::Rect{rect.x + 4.0f, bounds.y + 4.0f,
                               rect.width - 8.0f, bounds.height - 8.0f};
            renderer.FillRect(wash, tokens.hover, tokens.radius_medium);
        }
    }

    // Active indicator: sliding chip (Pill) or bottom bar (Underline).
    if (active_index_ >= 0 && active_index_ < static_cast<int>(tab_rects_.size())) {
        if (pill) {
            const auto chip = PillRect(ind_x_.pos, ind_w_.pos, bounds);
            // Filled accent chip on the active tab — the inactive
            // tabs stay ghost (muted label, hover wash only).
            renderer.FillRect(chip, tokens.accent, chip.height * 0.5f);
            // Focus ring hugs the active chip when the bar has keyboard focus.
            if (IsFocused()) {
                renderer.StrokeRect(
                    Layout::Rect{chip.x - 1.5f, chip.y - 1.5f,
                                 chip.width + 3.0f, chip.height + 3.0f},
                    tokens.ring, 1.5f, chip.height * 0.5f + 1.5f);
            }
        } else {
            // NLE chrome: the active tab is marked by the accent bar
            // (cyan in the video-editor theme), not a foreground bar.
            renderer.FillRect(
                Layout::Rect{ind_x_.pos, bounds.y + bounds.height - 2.0f,
                             ind_w_.pos, 2.0f},
                tokens.accent,
                1.0f
            );
        }
    }

    // Press feedback: a quick darkening on the held tab.
    if (press > 0.001f && pressed_tab_ >= 0 &&
        pressed_tab_ < static_cast<int>(tab_rects_.size())) {
        const auto& rect = tab_rects_[static_cast<std::size_t>(pressed_tab_)];
        const auto area = pill
            ? PillRect(rect.x, rect.width, bounds)
            : Layout::Rect{rect.x, bounds.y, rect.width, bounds.height};
        Theme::Color dim = tokens.background;
        dim.a = 0.35f * press;
        renderer.FillRect(area, dim, tokens.radius_medium);
    }

    // Labels on top — each centered inside its own tab cell.
    // (CenteredTextRect spans the whole widget bounds; using it per tab
    // would stack every label on top of the others.)
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const auto& rect = tab_rects_[i];
        const bool active = static_cast<int>(i) == active_index_;
        const bool hovered = static_cast<int>(i) == hovered_tab_;

        Theme::Color text_color =
            active ? (pill ? tokens.accent_foreground
                           : tokens.foreground)
                   : tokens.muted;
        if (!active && hovered && IsEnabled()) {
            text_color = tokens.foreground;
        }
        if (!IsEnabled()) {
            text_color = tokens.muted;
        }
        const Rendering::TextStyle style{text_color, tokens.font_body,
                                         active ? 500 : 400};
        const auto ts = renderer.MeasureText(labels_[i], style);
        // Compressed cells (Layout shrank them to fit the strip)
        // ellipsize the label instead of bleeding into the next cell.
        std::string shrunk;
        std::string_view shown = labels_[i];
        float draw_w = ts.width;
        if (ts.width > rect.width) {
            shrunk = EllipsizeText(renderer, labels_[i], style,
                                   rect.width);
            shown = shrunk;
            draw_w = shown.empty()
                         ? 0.0f
                         : renderer.MeasureText(shown, style).width;
        }
        const float tx =
            rect.x + std::max(0.0f, (rect.width - draw_w) * 0.5f);
        const float ty =
            rect.y + std::max(0.0f, (rect.height - ts.height) * 0.5f);
        renderer.DrawText(shown,
                          Layout::Rect{tx, ty,
                                       std::min(draw_w, rect.width),
                                       ts.height},
                          style);
    }

    // Hairline under the whole bar separates chrome from content.
    Theme::Color line = tokens.border;
    line.a *= 0.7f;
    renderer.DrawLine(
        Layout::Point{bounds.x, bounds.y + bounds.height - 0.5f},
        Layout::Point{bounds.x + bounds.width, bounds.y + bounds.height - 0.5f},
        line, 1.0f);
}

void Tabs::Tick(double dt_seconds) {
    if (!IsEffectivelyVisible()) {
        return;
    }
    if (tooltip_dirty_) {
        SetTooltip(pending_tooltip_);
        tooltip_dirty_ = false;
    }
    ind_x_.Update(static_cast<float>(dt_seconds));
    ind_w_.Update(static_cast<float>(dt_seconds));
    press_t_.Update(static_cast<float>(dt_seconds));
    if (!ind_x_.Settled() || !ind_w_.Settled() || press_t_.Active()) {
        Invalidate();
    }
}

void Tabs::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    tab_rects_.clear();
    float total = 0.0f;
    for (const auto& label : labels_) {
        total += TabWidth(label);
    }
    // Chrome-style compression: when the strip overflows, every cell
    // shrinks proportionally (labels ellipsize in Render) instead of
    // the tail tabs sliding out of reach past the right edge.
    const float scale =
        total > bounds.width && total > 0.0f
            ? std::max(0.0f, bounds.width) / total
            : 1.0f;
    float x = bounds.x;
    const float height = bounds.height;
    for (const auto& label : labels_) {
        const float width =
            std::max(18.0f, TabWidth(label) * scale);
        tab_rects_.push_back(Layout::Rect{x, bounds.y, width, height});
        x += width;
    }
    RetargetIndicator();
}

void Tabs::RetargetIndicator() {
    if (active_index_ < 0 || active_index_ >= static_cast<int>(tab_rects_.size())) {
        return;
    }
    const auto& r = tab_rects_[static_cast<std::size_t>(active_index_)];
    if (!indicator_init_) {
        ind_x_.Snap(r.x);
        ind_w_.Snap(r.width);
        indicator_init_ = true;
    } else {
        ind_x_.SetTarget(r.x);
        ind_w_.SetTarget(r.width);
    }
}

bool Tabs::HandleEvent(const Event& event) {
    if (!IsEnabled() || labels_.empty()) {
        return false;
    }
    switch (event.type) {
        case Event::Type::MouseMove: {
            const int hit = HitTab(event.position);
            if (hit != hovered_tab_) {
                hovered_tab_ = hit;
                SyncTabTooltip();
                Invalidate();
            }
            return hit >= 0;
        }
        case Event::Type::MouseDown:
            if (event.mouse_button == MouseButton::Left) {
                const int hit = HitTab(event.position);
                if (hit >= 0) {
                    pressed_tab_ = hit;
                    press_t_.Set(1.0f, 0.08f);
                    Invalidate();
                }
                return hit >= 0;
            }
            return false;
        case Event::Type::MouseUp:
            if (pressed_tab_ >= 0) {
                pressed_tab_ = -1;
                press_t_.Set(0.0f, 0.16f);
                Invalidate();
            }
            return false;
        case Event::Type::Click: {
            const int index = HitTab(event.position);
            if (index < 0) {
                return false;
            }
            ActivateTab(index);
            return true;
        }
        case Event::Type::KeyDown:
            if (IsFocused()) {
                int next = -1;
                switch (event.key) {
                    case KeyCode::ArrowLeft:
                        next = active_index_ > 0 ? active_index_ - 1 : 0;
                        break;
                    case KeyCode::ArrowRight:
                        next = active_index_ + 1 < static_cast<int>(labels_.size())
                                   ? active_index_ + 1
                                   : static_cast<int>(labels_.size()) - 1;
                        break;
                    case KeyCode::Home: next = 0; break;
                    case KeyCode::End:
                        next = static_cast<int>(labels_.size()) - 1;
                        break;
                    default: break;
                }
                if (next >= 0) {
                    ActivateTab(next);
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

int Tabs::HitTab(const Layout::Point& position) const {
    for (std::size_t i = 0; i < tab_rects_.size(); ++i) {
        if (tab_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Layout::Size Tabs::GetMinSize() const {
    return {0.0f, 36.0f};
}

Layout::Size Tabs::GetPreferredSize() const {
    float width = 0.0f;
    for (const auto& label : labels_) {
        width += TabWidth(label);
    }
    return {std::max(width, 0.0f), 36.0f};
}

Layout::Size Tabs::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
