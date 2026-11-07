#include "accordion.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr float kHeaderHeight = 40.0f;
constexpr float kContentPadding = 12.0f;

} // namespace

void Accordion::SetItems(std::vector<Item> items, bool single_open) {
    items_ = std::move(items);
    single_open_ = single_open;
    RebuildChildren();
    RequestLayout();
    Invalidate();
}

void Accordion::SetItemOpen(std::size_t index, bool open) {
    if (index >= items_.size() || items_[index].open == open) {
        return;
    }
    if (open && single_open_) {
        for (auto& item : items_) {
            item.open = false;
        }
    }
    items_[index].open = open;
    RebuildChildren();
    RequestLayout();
    Invalidate();
    if (on_toggled_) {
        on_toggled_(index, open);
    }
}

bool Accordion::IsItemOpen(std::size_t index) const {
    return index < items_.size() && items_[index].open;
}

void Accordion::SetOnToggled(std::function<void(std::size_t, bool)> callback) {
    on_toggled_ = std::move(callback);
}

void Accordion::RebuildChildren() {
    for (const auto& item : items_) {
        if (item.content != nullptr) {
            RemoveChild(item.content);
        }
    }
    for (const auto& item : items_) {
        if (item.open && item.content != nullptr) {
            AddChild(item.content);
        }
    }
}

void Accordion::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    float y = bounds.y;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const auto& header = header_rects_[i];
        const bool open = items_[i].open;
        const bool hovered = static_cast<int>(i) == hovered_header_;

        if (hovered && IsEnabled()) {
            renderer.FillRect(header, tokens.hover, 0.0f);
        }
        renderer.DrawLine(
            Layout::Point{header.x, header.y + header.height - 1.0f},
            Layout::Point{header.x + header.width, header.y + header.height - 1.0f},
            tokens.border,
            1.0f
        );

        const Rendering::TextStyle style{tokens.foreground, tokens.font_body, 500};
        const float text_height = renderer.MeasureText(items_[i].title, style).height;
        renderer.DrawText(
            items_[i].title,
            Layout::Rect{
                header.x + tokens.spacing_medium,
                header.y + std::max(0.0f, (header.height - text_height) * 0.5f),
                header.width - 40.0f,
                text_height
            },
            style
        );

        // Chevron: v open / > closed, drawn as two strokes.
        const float cx = header.x + header.width - 18.0f;
        const float cy = header.y + header.height * 0.5f;
        const Theme::Color chevron_color = tokens.muted;
        if (open) {
            renderer.DrawLine(
                Layout::Point{cx - 4.0f, cy - 2.0f},
                Layout::Point{cx, cy + 2.0f},
                chevron_color, 1.5f);
            renderer.DrawLine(
                Layout::Point{cx, cy + 2.0f},
                Layout::Point{cx + 4.0f, cy - 2.0f},
                chevron_color, 1.5f);
        } else {
            renderer.DrawLine(
                Layout::Point{cx - 4.0f, cy - 4.0f},
                Layout::Point{cx, cy},
                chevron_color, 1.5f);
            renderer.DrawLine(
                Layout::Point{cx, cy},
                Layout::Point{cx + 4.0f, cy - 4.0f},
                chevron_color, 1.5f);
        }

        if (open && i < content_heights_.size()) {
            y += content_heights_[i];
        }
    }
    // Bottom border.
    renderer.DrawLine(
        Layout::Point{bounds.x, bounds.y + bounds.height - 1.0f},
        Layout::Point{bounds.x + bounds.width, bounds.y + bounds.height - 1.0f},
        tokens.border,
        1.0f
    );
}

void Accordion::Layout(const Layout::Rect& bounds) {
    Layout(bounds, Theme::Tokens{});
}

void Accordion::Layout(const Layout::Rect& bounds, const Theme::Tokens&) {
    SetBounds(bounds);
    header_rects_.clear();
    content_heights_.assign(items_.size(), 0.0f);

    float y = bounds.y;
    for (std::size_t i = 0; i < items_.size(); ++i) {
        header_rects_.push_back(Layout::Rect{bounds.x, y, bounds.width, kHeaderHeight});
        y += kHeaderHeight;
        if (items_[i].open && items_[i].content != nullptr) {
            const float content_height = items_[i].content->GetPreferredSize().height + 2.0f * kContentPadding;
            content_heights_[i] = content_height;
            items_[i].content->Layout(Layout::Rect{
                bounds.x + kContentPadding,
                y + kContentPadding,
                std::max(0.0f, bounds.width - 2.0f * kContentPadding),
                content_height - 2.0f * kContentPadding
            });
            y += content_height;
        }
    }
}

bool Accordion::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    if (event.type == Event::Type::Click) {
        const auto index = HitHeader(event.position);
        if (index != items_.size()) {
            SetItemOpen(index, !items_[index].open);
            return true;
        }
    }
    if (event.type == Event::Type::MouseMove) {
        const auto hit = HitHeader(event.position);
        const int as_int = hit == items_.size() ? -1 : static_cast<int>(hit);
        if (as_int != hovered_header_) {
            hovered_header_ = as_int;
            Invalidate();
        }
        return as_int >= 0;
    }
    return false;
}

std::size_t Accordion::HitHeader(const Layout::Point& position) const {
    for (std::size_t i = 0; i < header_rects_.size(); ++i) {
        if (header_rects_[i].Contains(position)) {
            return i;
        }
    }
    return items_.size();
}

Layout::Size Accordion::GetMinSize() const {
    return {0.0f, kHeaderHeight};
}

Layout::Size Accordion::GetPreferredSize() const {
    float height = kHeaderHeight * static_cast<float>(items_.size());
    for (const auto& item : items_) {
        if (item.open && item.content != nullptr) {
            height += item.content->GetPreferredSize().height + 2.0f * kContentPadding;
        }
    }
    return {0.0f, height};
}

Layout::Size Accordion::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
