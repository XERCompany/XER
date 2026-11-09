#include "text_input.h"

#include <algorithm>
#include <utility>

#include "content/clipboard/clipboard.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"

namespace XER::UI::Widgets {
namespace {

constexpr std::size_t kMaxUndoSteps = 100;

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

std::size_t PreviousCodePoint(std::string_view text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 && IsContinuation(static_cast<unsigned char>(text[position]))) {
        --position;
    }
    return position;
}

std::size_t NextCodePoint(std::string_view text, std::size_t position) {
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
    selection_anchor_ = caret_;
    SetFocusable(true);
}

const std::string& TextInput::GetValue() const {
    return value_;
}

void TextInput::SetValue(std::string value) {
    if (value_ == value) {
        return;
    }
    PushUndo();
    value_ = std::move(value);
    caret_ = value_.size();
    selection_anchor_ = caret_;
    coalesce_typing_ = false;
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

void TextInput::SetPasswordMode(bool enabled) {
    if (password_mode_ == enabled) {
        return;
    }
    password_mode_ = enabled;
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
    renderer.StrokeRect(bounds, IsFocused() ? tokens.ring : tokens.border,
                        1.0f, tokens.radius_small);

    const Rendering::TextStyle style{
        IsEnabled() ? tokens.foreground : tokens.muted,
        tokens.font_body,
        400
    };
    const std::string display = DisplayText(value_);
    const auto text_bounds = CenteredTextRect(
        renderer, display, style, kHorizontalPadding);

    if (pending_caret_x_.has_value()) {
        caret_ = CaretIndexForX(*pending_caret_x_, renderer, text_bounds, style);
        selection_anchor_ = caret_;
        coalesce_typing_ = false;
        pending_caret_x_.reset();
    }

    const auto x_for = [&](std::size_t pos) {
        const auto prefix = DisplayText(std::string_view(value_).substr(0, pos));
        return std::clamp(
            text_bounds.x + renderer.MeasureText(prefix, style).width,
            text_bounds.x,
            text_bounds.x + text_bounds.width
        );
    };

    // Clip text + caret to the field: long values must not paint outside
    // the rounded input box.
    renderer.PushClip(bounds);
    if (IsFocused() && HasSelection()) {
        const auto [begin, end] = SelectionRange();
        const float x0 = x_for(begin);
        const float x1 = x_for(end);
        Theme::Color highlight = tokens.accent;
        highlight.a = 0.35f;
        renderer.FillRect({x0, text_bounds.y, x1 - x0, text_bounds.height},
                          highlight, 0.0f);
    }
    if (value_.empty() && !placeholder_.empty()) {
        const Rendering::TextStyle ph{tokens.muted, tokens.font_body, 400};
        renderer.DrawText(placeholder_, text_bounds, ph);
    } else {
        renderer.DrawText(display, text_bounds, style);
    }

    if (IsFocused()) {
        const float caret_x = x_for(caret_);
        renderer.DrawLine(
            {caret_x, text_bounds.y},
            {caret_x, text_bounds.y + text_bounds.height},
            tokens.foreground,
            1.0f
        );
    }
    renderer.PopClip();

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

    if (event.is_ctrl && !event.is_alt &&
        HandleShortcut(event.key, event.is_shift)) {
        return true;
    }

    const bool extend = event.is_shift;
    switch (event.key) {
    case KeyCode::Backspace:
        if (HasSelection()) {
            const auto [begin, end] = SelectionRange();
            EraseRange(begin, end);
            return true;
        }
        if (caret_ > 0) {
            EraseRange(PreviousCodePoint(value_, caret_), caret_);
            return true;
        }
        return false;
    case KeyCode::Delete:
        if (HasSelection()) {
            const auto [begin, end] = SelectionRange();
            EraseRange(begin, end);
            return true;
        }
        if (caret_ < value_.size()) {
            EraseRange(caret_, NextCodePoint(value_, caret_));
            return true;
        }
        return false;
    case KeyCode::ArrowLeft:
        if (HasSelection() && !extend) {
            MoveCaret(SelectionRange().first, false);
            return true;
        }
        if (caret_ > 0) {
            MoveCaret(PreviousCodePoint(value_, caret_), extend);
            return true;
        }
        return false;
    case KeyCode::ArrowRight:
        if (HasSelection() && !extend) {
            MoveCaret(SelectionRange().second, false);
            return true;
        }
        if (caret_ < value_.size()) {
            MoveCaret(NextCodePoint(value_, caret_), extend);
            return true;
        }
        return false;
    case KeyCode::Home:
        MoveCaret(0, extend);
        return true;
    case KeyCode::End:
        MoveCaret(value_.size(), extend);
        return true;
    default:
        return false;
    }
}

bool TextInput::HandleShortcut(KeyCode key, bool shift) {
    switch (key) {
    case KeyCode::A:
        selection_anchor_ = 0;
        caret_ = value_.size();
        coalesce_typing_ = false;
        Invalidate();
        return true;
    case KeyCode::C:
    case KeyCode::X: {
        // Never put a masked password on the clipboard.
        if (!HasSelection() || password_mode_) {
            return true;
        }
        const auto [begin, end] = SelectionRange();
        ::XER::Content::Clipboard::SetText(
            std::string_view(value_).substr(begin, end - begin));
        if (key == KeyCode::X) {
            EraseRange(begin, end);
        }
        return true;
    }
    case KeyCode::V: {
        std::string text;
        if (!::XER::Content::Clipboard::GetText(text)) {
            return true;
        }
        // Single-line field: drop line breaks.
        std::erase_if(text, [](char c) { return c == '\r' || c == '\n'; });
        coalesce_typing_ = false;
        InsertText(text);
        coalesce_typing_ = false;
        return true;
    }
    case KeyCode::Z:
        shift ? Redo() : Undo();
        return true;
    case KeyCode::Y:
        Redo();
        return true;
    default:
        return false;
    }
}

bool TextInput::HasSelection() const {
    return selection_anchor_ != caret_;
}

std::pair<std::size_t, std::size_t> TextInput::SelectionRange() const {
    return {std::min(selection_anchor_, caret_),
            std::max(selection_anchor_, caret_)};
}

bool TextInput::DeleteSelection() {
    if (!HasSelection()) {
        return false;
    }
    const auto [begin, end] = SelectionRange();
    value_.erase(begin, end - begin);
    caret_ = selection_anchor_ = begin;
    return true;
}

void TextInput::EraseRange(std::size_t begin, std::size_t end) {
    PushUndo();
    value_.erase(begin, end - begin);
    caret_ = selection_anchor_ = begin;
    coalesce_typing_ = false;
    NotifyValueChanged();
}

void TextInput::MoveCaret(std::size_t position, bool extend_selection) {
    caret_ = std::min(position, value_.size());
    if (!extend_selection) {
        selection_anchor_ = caret_;
    }
    coalesce_typing_ = false;
    Invalidate();
}

void TextInput::PushUndo() {
    undo_stack_.push_back({value_, caret_, selection_anchor_});
    if (undo_stack_.size() > kMaxUndoSteps) {
        undo_stack_.erase(undo_stack_.begin());
    }
    redo_stack_.clear();
}

bool TextInput::Undo() {
    if (undo_stack_.empty()) {
        return false;
    }
    redo_stack_.push_back({value_, caret_, selection_anchor_});
    EditState state = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    value_ = std::move(state.value);
    caret_ = std::min(state.caret, value_.size());
    selection_anchor_ = std::min(state.anchor, value_.size());
    coalesce_typing_ = false;
    NotifyValueChanged();
    return true;
}

bool TextInput::Redo() {
    if (redo_stack_.empty()) {
        return false;
    }
    undo_stack_.push_back({value_, caret_, selection_anchor_});
    EditState state = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    value_ = std::move(state.value);
    caret_ = std::min(state.caret, value_.size());
    selection_anchor_ = std::min(state.anchor, value_.size());
    coalesce_typing_ = false;
    NotifyValueChanged();
    return true;
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
    if (!coalesce_typing_ || HasSelection()) {
        PushUndo();
    }
    DeleteSelection();
    value_.insert(caret_, text);
    caret_ += text.size();
    selection_anchor_ = caret_;
    // A space ends the current undo word.
    coalesce_typing_ = text != " ";
    NotifyValueChanged();
}

std::string TextInput::DisplayText(std::string_view value) const {
    if (!password_mode_) {
        return std::string(value);
    }
    // One bullet per Unicode code point so the masked width tracks the
    // real text regardless of UTF-8 sequence length.
    std::string out;
    for (std::size_t i = 0; i < value.size();) {
        i = NextCodePoint(value, i);
        out += "\xE2\x80\xA2";  // U+2022 BULLET
    }
    return out;
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
        // Measure the *display* prefix (bullets in password mode) so click
        // hit-testing matches what is actually painted.
        const float width = renderer.MeasureText(
            DisplayText(std::string_view(value_).substr(0, pos)),
            style).width;
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
