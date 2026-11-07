#include "button.h"

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

Button::Button(std::string text, ButtonStyle style) 
    : text_(std::move(text))
    , style_(style) {
    SetFocusable(true);
}

void Button::SetText(std::string text) {
    text_ = std::move(text);
    Invalidate();
    RequestLayout();
}

const std::string& Button::GetText() const {
    return text_;
}

void Button::SetOnClick(std::function<void()> callback) {
    on_click_ = std::move(callback);
}

void Button::SetStyle(ButtonStyle style) {
    style_ = style;
    Invalidate();
}

void Button::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    
    Theme::Color fill_color;
    Theme::Color stroke_color;
    Theme::Color text_color;
    float stroke_width = 1.0f;

    if (!IsEnabled()) {
        fill_color = tokens.muted;
        stroke_color = tokens.border;
        text_color = tokens.background;
    } else {
        switch (style_) {
            case ButtonStyle::Primary:
                fill_color = IsFocused() ? tokens.focus : tokens.foreground;
                stroke_color = fill_color;
                text_color = tokens.surface;
                stroke_width = 0.0f;
                break;
            case ButtonStyle::Secondary:
                fill_color = tokens.surface;
                stroke_color = IsFocused() ? tokens.focus : tokens.border;
                text_color = tokens.foreground;
                break;
            case ButtonStyle::Outline:
                fill_color = tokens.background;
                stroke_color = IsFocused() ? tokens.focus : tokens.border;
                text_color = tokens.foreground;
                break;
        }
    }

    renderer.FillRect(bounds, fill_color, tokens.radius_medium);
    if (stroke_width > 0.0f) {
        renderer.StrokeRect(bounds, stroke_color, stroke_width, tokens.radius_medium);
    }
    renderer.DrawText(
        text_,
        Layout::EdgeInsets(tokens.spacing_large, tokens.spacing_small)
            .Apply(bounds),
        Rendering::TextStyle{
            text_color,
            tokens.font_body,
            true
        }
    );
}

void Button::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Button::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    if (event.type == Event::Type::Click && event.mouse_button == MouseButton::Left) {
        Activate();
        return true;
    }
    if (event.type == Event::Type::KeyDown && IsFocused() &&
        (event.key == KeyCode::Enter || event.key == KeyCode::Space)) {
        Activate();
        return true;
    }
    return false;
}

Layout::Size Button::GetMinSize() const {
    return {64.0f, 20.0f + 2.0f * 4.0f};
}

Layout::Size Button::GetPreferredSize() const {
    return {
        std::max(GetMinSize().width, static_cast<float>(Utf8Length(text_)) * 7.7f + 32.0f),
        20.0f + 2.0f * 4.0f
    };
}

Layout::Size Button::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void Button::Activate() {
    if (on_click_) {
        on_click_();
    }
}

} // namespace XER::UI::Widgets
