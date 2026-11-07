#include "toggle.h"

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

// ---------------- Toggle ----------------

Toggle::Toggle(std::string text, bool pressed)
    : text_(std::move(text))
    , pressed_(pressed) {
    SetFocusable(true);
}

void Toggle::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

void Toggle::SetPressed(bool pressed) {
    if (pressed_ == pressed) {
        return;
    }
    pressed_ = pressed;
    Invalidate();
    if (on_pressed_changed_) {
        on_pressed_changed_(pressed_);
    }
}

void Toggle::SetOnPressedChanged(std::function<void(bool)> callback) {
    on_pressed_changed_ = std::move(callback);
}

void Toggle::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();

    Theme::Color fill = pressed_ ? tokens.secondary : tokens.surface;
    Theme::Color stroke = pressed_ ? tokens.foreground : tokens.border;
    Theme::Color text_color = pressed_ ? tokens.foreground : tokens.foreground;
    if (IsHovered() && IsEnabled()) {
        fill = pressed_ ? tokens.secondary : tokens.hover;
    }
    if (!IsEnabled()) {
        fill = tokens.muted;
        stroke = tokens.border;
        text_color = tokens.background;
    }

    renderer.FillRect(bounds, fill, tokens.radius_medium);
    renderer.StrokeRect(bounds, stroke, 1.0f, tokens.radius_medium);

    const Rendering::TextStyle style{text_color, tokens.font_body, pressed_ ? 500 : 400};
    renderer.DrawText(text_, CenteredTextRect(renderer, text_, style, tokens.spacing_medium), style);
    RenderFocusRing(renderer, tokens, tokens.radius_medium);
}

void Toggle::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Toggle::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    if (event.type == Event::Type::Click) {
        SetPressed(!pressed_);
        return true;
    }
    if (event.type == Event::Type::KeyDown &&
        (event.key == KeyCode::Space || event.key == KeyCode::Enter)) {
        SetPressed(!pressed_);
        return true;
    }
    return false;
}

Layout::Size Toggle::GetMinSize() const {
    return {0.0f, 32.0f};
}

Layout::Size Toggle::GetPreferredSize() const {
    return {static_cast<float>(Utf8Length(text_)) * 7.0f + 24.0f, 32.0f};
}

Layout::Size Toggle::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

// ---------------- ToggleGroup ----------------

void ToggleGroup::SetOptions(std::vector<std::string> options, bool allow_multiple) {
    options_ = std::move(options);
    allow_multiple_ = allow_multiple;
    selected_.clear();
    RequestLayout();
    Invalidate();
}

void ToggleGroup::SetSelectedIndices(std::vector<int> indices) {
    selected_ = std::move(indices);
    Invalidate();
}

bool ToggleGroup::IsSelected(int index) const {
    return std::find(selected_.begin(), selected_.end(), index) != selected_.end();
}

void ToggleGroup::SetOnChanged(std::function<void(const std::vector<int>&)> callback) {
    on_changed_ = std::move(callback);
}

void ToggleGroup::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    for (std::size_t i = 0; i < options_.size(); ++i) {
        const auto& rect = option_rects_[i];
        const bool active = IsSelected(static_cast<int>(i));
        const bool first = i == 0;
        const bool last = i + 1 == options_.size();

        Theme::Color fill = active ? tokens.secondary : tokens.surface;
        Theme::Color stroke = active ? tokens.foreground : tokens.border;
        if (IsEnabled()) {
            renderer.FillRect(rect, fill, 0.0f);
            renderer.StrokeRect(rect, stroke, 1.0f, 0.0f);
        } else {
            renderer.FillRect(rect, tokens.muted, 0.0f);
            renderer.StrokeRect(rect, tokens.border, 1.0f, 0.0f);
        }
        // Inner separators between options.
        if (!first) {
            renderer.DrawLine(
                Layout::Point{rect.x, rect.y},
                Layout::Point{rect.x, rect.y + rect.height},
                tokens.border,
                1.0f
            );
        }
        (void)last;

        const Rendering::TextStyle style{tokens.foreground, tokens.font_body, active ? 500 : 400};
        const float text_height = renderer.MeasureText(options_[i], style).height;
        const Layout::Rect text_rect{
            rect.x,
            rect.y + std::max(0.0f, (rect.height - text_height) * 0.5f),
            rect.width,
            text_height
        };
        renderer.DrawText(options_[i], text_rect, style);
    }
}

void ToggleGroup::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
    option_rects_.clear();
    if (options_.empty()) {
        return;
    }
    const float width = bounds.width / static_cast<float>(options_.size());
    for (std::size_t i = 0; i < options_.size(); ++i) {
        option_rects_.push_back(Layout::Rect{
            bounds.x + width * static_cast<float>(i),
            bounds.y,
            width,
            bounds.height
        });
    }
}

bool ToggleGroup::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }
    if (event.type == Event::Type::Click) {
        const int index = HitOption(event.position);
        if (index >= 0) {
            ToggleIndex(index);
            return true;
        }
    }
    return false;
}

int ToggleGroup::HitOption(const Layout::Point& position) const {
    for (std::size_t i = 0; i < option_rects_.size(); ++i) {
        if (option_rects_[i].Contains(position)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ToggleGroup::ToggleIndex(int index) {
    auto it = std::find(selected_.begin(), selected_.end(), index);
    if (it != selected_.end()) {
        if (allow_multiple_) {
            selected_.erase(it);
        } else if (selected_.size() > 1) {
            selected_.erase(it);
        }
    } else {
        if (!allow_multiple_) {
            selected_.clear();
        }
        selected_.push_back(index);
    }
    std::sort(selected_.begin(), selected_.end());
    Invalidate();
    if (on_changed_) {
        on_changed_(selected_);
    }
}

Layout::Size ToggleGroup::GetMinSize() const {
    return {0.0f, 32.0f};
}

Layout::Size ToggleGroup::GetPreferredSize() const {
    float width = 0.0f;
    for (const auto& option : options_) {
        width += static_cast<float>(Utf8Length(option)) * 7.0f + 24.0f;
    }
    return {std::max(width, 0.0f), 32.0f};
}

Layout::Size ToggleGroup::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
