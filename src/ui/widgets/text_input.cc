#include "text_input.h"

#include <algorithm>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

std::size_t PreviousCodePoint(const std::string& text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 && IsContinuation(static_cast<unsigned char>(text[position]))) {
        --position;
    }
    return position;
}

std::size_t NextCodePoint(const std::string& text, std::size_t position) {
    if (position >= text.size()) {
        return text.size();
    }
    ++position;
    while (position < text.size() &&
           IsContinuation(static_cast<unsigned char>(text[position]))) {
        ++position;
    }
    return position;
}

} // namespace

TextInput::TextInput(std::string value) : value_(std::move(value)), caret_(value_.size()) {
    SetFocusable(true);
}

const std::string& TextInput::GetValue() const {
    return value_;
}

void TextInput::SetValue(std::string value) {
    if (value_ == value) {
        return;
    }
    value_ = std::move(value);
    caret_ = value_.size();
    NotifyValueChanged();
    RequestLayout();
}

void TextInput::SetOnValueChanged(
    std::function<void(const std::string&)> callback
) {
    on_value_changed_ = std::move(callback);
}

void TextInput::SetPlaceholder(std::string placeholder) {
    if (placeholder_ == placeholder) {
        return;
    }
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void TextInput::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    constexpr float kHorizontalPadding = 12.0f;
    renderer.FillRect(
        bounds,
        IsEnabled() ? tokens.surface : tokens.secondary,
        tokens.radius_small
    );
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_small);

    const Rendering::TextStyle style{
        IsEnabled() ? tokens.foreground : tokens.muted,
        tokens.font_body,
        400
    };
    const auto text_bounds = CenteredTextRect(
        renderer, value_, style, kHorizontalPadding);

    if (pending_caret_x_.has_value()) {
        caret_ = CaretIndexForX(*pending_caret_x_, renderer, text_bounds, style);
        pending_caret_x_.reset();
    }

    if (value_.empty() && !placeholder_.empty() && !IsFocused()) {
        const Rendering::TextStyle ph{tokens.muted, tokens.font_body, 400};
        renderer.DrawText(placeholder_, text_bounds, ph);
    } else {
        renderer.DrawText(value_, text_bounds, style);
    }

    if (IsFocused()) {
        const auto prefix = std::string_view(value_).substr(0, caret_);
        const float caret_x = std::clamp(
            text_bounds.x + renderer.MeasureText(prefix, style).width,
            text_bounds.x,
            text_bounds.x + text_bounds.width
        );
        renderer.DrawLine(
            {caret_x, text_bounds.y},
            {caret_x, text_bounds.y + text_bounds.height},
            tokens.foreground,
            1.0f
        );
    }

    RenderFocusRing(renderer, tokens, tokens.radius_small);
}

void TextInput::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool TextInput::HandleEvent(const Event& event) {
    if (!IsEnabled()) {
        return false;
    }

    if (event.type == Event::Type::MouseDown &&
        event.mouse_button == MouseButton::Left && IsFocused()) {
        pending_caret_x_ = event.position.x;
        Invalidate();
        return true;
    }
    if (!IsFocused()) {
        return false;
    }

    if (event.type == Event::Type::TextInput) {
        InsertText(event.text);
        return !event.text.empty();
    }
    if (event.type != Event::Type::KeyDown) {
        return false;
    }

    switch (event.key) {
    case KeyCode::Backspace:
        if (caret_ > 0) {
            const auto previous = PreviousCodePoint(value_, caret_);
            value_.erase(previous, caret_ - previous);
            caret_ = previous;
            NotifyValueChanged();
            return true;
        }
        return false;
    case KeyCode::Delete:
        if (caret_ < value_.size()) {
            const auto next = NextCodePoint(value_, caret_);
            value_.erase(caret_, next - caret_);
            NotifyValueChanged();
            return true;
        }
        return false;
    case KeyCode::ArrowLeft:
        if (caret_ > 0) {
            caret_ = PreviousCodePoint(value_, caret_);
            Invalidate();
            return true;
        }
        return false;
    case KeyCode::ArrowRight:
        if (caret_ < value_.size()) {
            caret_ = NextCodePoint(value_, caret_);
            Invalidate();
            return true;
        }
        return false;
    case KeyCode::Home:
        if (caret_ != 0) {
            caret_ = 0;
            Invalidate();
        }
        return true;
    case KeyCode::End:
        if (caret_ != value_.size()) {
            caret_ = value_.size();
            Invalidate();
        }
        return true;
    default:
        return false;
    }
}

Layout::Size TextInput::GetMinSize() const {
    return {80.0f, 36.0f};
}

Layout::Size TextInput::GetPreferredSize() const {
    return {160.0f, 36.0f};
}

Layout::Size TextInput::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

void TextInput::InsertText(const std::string& text) {
    if (text.empty()) {
        return;
    }
    value_.insert(caret_, text);
    caret_ += text.size();
    NotifyValueChanged();
}

void TextInput::NotifyValueChanged() {
    Invalidate();
    if (on_value_changed_) {
        on_value_changed_(value_);
    }
}

std::size_t TextInput::CaretIndexForX(
    float x,
    Rendering::Renderer& renderer,
    const Layout::Rect& text_bounds,
    const Rendering::TextStyle& style
) const {
    const float local_x = x - text_bounds.x;
    if (local_x <= 0.0f) {
        return 0;
    }
    for (std::size_t pos = 0; pos <= value_.size();) {
        const float width = renderer.MeasureText(
            std::string_view(value_).substr(0, pos), style).width;
        if (width > local_x) {
            return pos == 0 ? 0 : PreviousCodePoint(value_, pos);
        }
        if (pos == value_.size()) {
            break;
        }
        pos = NextCodePoint(value_, pos);
    }
    return value_.size();
}

} // namespace XER::UI::Widgets
