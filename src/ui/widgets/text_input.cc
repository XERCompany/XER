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

void TextInput::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_small);
    renderer.StrokeRect(
        bounds,
        IsFocused() ? tokens.focus : tokens.border,
        1.0f,
        tokens.radius_small
    );
    const Layout::Rect text_bounds{
        bounds.x + tokens.spacing_medium,
        bounds.y,
        std::max(0.0f, bounds.width - 2.0f * tokens.spacing_medium),
        bounds.height
    };
    const Rendering::TextStyle style{
        IsEnabled() ? tokens.foreground : tokens.muted,
        tokens.font_body,
        false
    };
    renderer.DrawText(value_, text_bounds, style);

    if (IsFocused()) {
        const auto prefix = std::string_view(value_).substr(0, caret_);
        const float caret_x = text_bounds.x + renderer.MeasureText(prefix, style).width;
        renderer.DrawLine(
            {caret_x, bounds.y + tokens.spacing_small},
            {caret_x, bounds.y + bounds.height - tokens.spacing_small},
            tokens.focus,
            1.0f
        );
    }
}

void TextInput::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool TextInput::HandleEvent(const Event& event) {
    if (!IsEnabled() || !IsFocused()) {
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
    return {80.0f, 18.0f + 2.0f * 8.0f};
}

Layout::Size TextInput::GetPreferredSize() const {
    return {160.0f, 18.0f + 2.0f * 8.0f};
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

} // namespace XER::UI::Widgets
