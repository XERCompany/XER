#include "radio_group.h"

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

void RadioGroup::SetOptions(std::vector<std::string> options) {
    options_ = std::move(options);
    if (selected_index_ >= static_cast<int>(options_.size())) {
        selected_index_ = options_.empty() ? -1 : 0;
    }
    RequestLayout();
    Invalidate();
}

void RadioGroup::SetSelectedIndex(int index) {
    if (index < -1 || index >= static_cast<int>(options_.size()) || index == selected_index_) {
        return;
    }
    selected_index_ = index;
    Invalidate();
    if (on_changed_) {
        on_changed_(index);
    }
}

void RadioGroup::SetOnChanged(std::function<void(int)> callback) {
    on_changed_ = std::move(callback);
}

void RadioGroup::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const auto& rect = option_rects_[i];
        const bool selected = static_cast<int>(i) == selected_index_;
        const bool hovered = static_cast<int>(i) == hovered_option_;

        if (hovered && IsEnabled()) {
            renderer.FillRect(rect, tokens.hover, tokens.radius_small);
        }

        const float cy = rect.y + rect.height * 0.5f;
        const Layout::Rect indicator{rect.x + 4.0f, cy - 8.0f, 16.0f, 16.0f};
        renderer.StrokeRect(indicator, tokens.border, 1.5f, 8.0f);
        if (selected) {
            renderer.FillRect(
                Layout::Rect{indicator.x + 4.0f, indicator.y + 4.0f, 8.0f, 8.0f},
                tokens.foreground,
                4.0f
            );
        }

        const Rendering::TextStyle style{tokens.foreground, tokens.font_body, 400};
        const float text_height = renderer.MeasureText(options_[i], style).height;
        const Layout::Rect text_rect{
            rect.x + 28.0f,
            rect.y + std::max(0.0f, (rect.height - text_height) * 0.5f),
            rect.width - 28.0f,
            text_height
        };
        renderer.DrawText(options_[i], text_rect, style);
    }
}

void RadioGroup::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    option_rects_.clear();
    float y = bounds.y;
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const float h = 28.0f;
        option_rects_.push_back(Layout::Rect{bounds.x, y, bounds.width, h});
        y += h;
    }
}

bool RadioGroup::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    switch (event.type) {
        case Event::Type::MouseMove: {
            const int hit = HitOption(event.position);
            if (hit != hovered_option_) {
                hovered_option_ = hit;
                Invalidate();
            }
            return hit >= 0;
        }
        case Event::Type::Click: {
            const int index = HitOption(event.position);
            if (index < 0) {
                return false;
            }
            SetSelectedIndex(index);
            return true;
        }
        case Event::Type::KeyDown:
            if (event.key == KeyCode::ArrowDown || event.key == KeyCode::ArrowRight) {
                const int next = selected_index_ < 0 ? 0
                    : std::min(selected_index_ + 1, static_cast<int>(options_.size()) - 1);
                SetSelectedIndex(next);
                return true;
            }
            if (event.key == KeyCode::ArrowUp || event.key == KeyCode::ArrowLeft) {
                const int prev = selected_index_ <= 0 ? 0 : selected_index_ - 1;
                SetSelectedIndex(prev);
                return true;
            }
            return false;
        default:
            return false;
    }
}

int RadioGroup::HitOption(const Layout::Point& position) const {
    for (std::size_t i = 0; i < option_rects_.size(); ++i) {
        if (option_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

Layout::Size RadioGroup::GetMinSize() const {
    return {0.0f, 28.0f};
}

Layout::Size RadioGroup::GetPreferredSize() const {
    float width = 0.0f;
    for (const auto& option : options_) {
        width = std::max(width, static_cast<float>(Utf8Length(option)) * 7.0f + 36.0f);
    }
    return {width, 28.0f * static_cast<float>(options_.size())};
}

Layout::Size RadioGroup::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
