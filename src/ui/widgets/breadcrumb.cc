#include "breadcrumb.h"

#include <algorithm>
#include <utility>

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

} // namespace

void Breadcrumb::SetItems(std::vector<std::string> items) {
    items_ = std::move(items);
    RequestLayout();
    Invalidate();
}

void Breadcrumb::SetOnNavigate(std::function<void(int)> callback) {
    on_navigate_ = std::move(callback);
}

void Breadcrumb::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const Rendering::TextStyle muted_style{tokens.muted, tokens.font_body, 400};
    const Rendering::TextStyle active_style{tokens.foreground, tokens.font_body, 500};
    const Rendering::TextStyle chevron_style{tokens.muted, tokens.font_body, 400};

    float x = bounds.x;
    const float cy = bounds.y + bounds.height * 0.5f;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const bool last = i + 1 == items_.size();
        const auto& style = last ? active_style : muted_style;
        const auto size = renderer.MeasureText(items_[i], style);
        const Layout::Rect text_rect{x, cy - size.height * 0.5f, size.width, size.height};

        if (!last && static_cast<int>(i) == hovered_item_ && IsEnabled()) {
            renderer.FillRect(
                Layout::Rect{text_rect.x - 4.0f, bounds.y, text_rect.width + 8.0f, bounds.height},
                tokens.hover,
                tokens.radius_small
            );
        }
        if (!last && !IsEnabled()) {
            // disabled: no hover emphasis
        }
        renderer.DrawText(items_[i], text_rect, style);
        x += size.width;

        if (!last) {
            // Chevron separator ">".
            const auto chevron_size = renderer.MeasureText(">", chevron_style);
            renderer.DrawText(
                ">",
                Layout::Rect{x + 6.0f, cy - chevron_size.height * 0.5f, chevron_size.width, chevron_size.height},
                chevron_style
            );
            x += 6.0f + chevron_size.width + 6.0f;
        }
    }
}

void Breadcrumb::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    // Item rects are approximated for hit-testing: recompute widths.
    item_rects_.clear();
    // Widths depend on text metrics which need a renderer; store generous
    // fixed estimates — refined on next layout pass by consumers that care.
    float x = bounds.x;
    for (const auto& item : items_) {
        const float w = static_cast<float>(Utf8Length(item)) * 7.0f;
        item_rects_.push_back(Layout::Rect{x, bounds.y, w, bounds.height});
        x += w + 24.0f; // separator allowance
    }
}

bool Breadcrumb::HandleEvent(const Event& event) {
    if (!IsEnabled() || on_navigate_ == nullptr) {
        return false;
    }
    if (event.type == Event::Type::MouseMove) {
        const int hit = HitItem(event.position);
        if (hit != hovered_item_) {
            hovered_item_ = hit;
            Invalidate();
        }
        return hit >= 0;
    }
    if (event.type == Event::Type::Click) {
        const int hit = HitItem(event.position);
        if (hit >= 0 && hit + 1 < static_cast<int>(items_.size())) {
            on_navigate_(hit);
            return true;
        }
    }
    return false;
}

int Breadcrumb::HitItem(const Layout::Point& position) const {
    for (std::size_t i = 0; i < item_rects_.size(); ++i) {
        if (item_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Layout::Size Breadcrumb::GetMinSize() const {
    return {0.0f, 24.0f};
}

Layout::Size Breadcrumb::GetPreferredSize() const {
    float width = 0.0f;
    for (const auto& item : items_) {
        width += static_cast<float>(Utf8Length(item)) * 7.0f + 24.0f;
    }
    return {std::max(0.0f, width - 24.0f), 24.0f};
}

Layout::Size Breadcrumb::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
