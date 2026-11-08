#include "textarea.h"

#include <algorithm>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "helper/string_utils.h"

namespace XER::UI::Widgets {
namespace {

std::vector<std::string> SplitLines(const std::string& text) {
    auto lines = XER::Helper::String::SplitLines(text);
    if (lines.empty()) {
        lines.emplace_back();
    }
    return lines;
}

} // namespace

Textarea::Textarea(std::string text)
    : text_(std::move(text)) {
    SetFocusable(true);
}

void Textarea::SetText(std::string text) {
    if (text_ == text) {
        return;
    }
    text_ = std::move(text);
    ClampCursor();
    Invalidate();
}

void Textarea::SetPlaceholder(std::string placeholder) {
    placeholder_ = std::move(placeholder);
    Invalidate();
}

void Textarea::SetReadOnly(bool read_only) {
    read_only_ = read_only;
    if (read_only_) {
        SetFocused(false);
    }
    Invalidate();
}

void Textarea::SetOnChanged(std::function<void(const std::string&)> callback) {
    on_changed_ = std::move(callback);
}

void Textarea::Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) {
    const auto bounds = GetBounds();
    const bool hovered = IsHovered();

    Theme::Color stroke = tokens.border;
    if (IsFocused()) {
        stroke = tokens.foreground;
    } else if (hovered && IsEnabled()) {
        stroke = tokens.muted;
    }
    renderer.FillRect(bounds, tokens.surface, tokens.radius_medium);
    renderer.StrokeRect(bounds, stroke, IsFocused() ? 1.5f : 1.0f, tokens.radius_medium);

    const Rendering::TextStyle style{tokens.foreground, tokens.font_body, 400};
    const Rendering::TextStyle placeholder_style{tokens.muted, tokens.font_body, 400};
    const float pad = tokens.spacing_small;
    const float line_height = tokens.font_body * 1.4f;

    const auto lines = SplitLines(text_);
    const int first_line = scroll_line_;
    const int max_lines = std::max(1, static_cast<int>(bounds.height / line_height) - 1);

    if (text_.empty() && !placeholder_.empty() && !IsFocused()) {
        renderer.DrawText(
            placeholder_,
            Layout::Rect{bounds.x + pad, bounds.y + pad, bounds.width - 2.0f * pad, line_height},
            placeholder_style
        );
    } else {
        for (int i = 0; i < max_lines && first_line + i < static_cast<int>(lines.size()); ++i) {
            const auto& line = lines[static_cast<std::size_t>(first_line + i)];
            if (line.empty()) {
                continue;
            }
            renderer.DrawText(
                line,
                Layout::Rect{
                    bounds.x + pad,
                    bounds.y + pad + static_cast<float>(i) * line_height,
                    bounds.width - 2.0f * pad,
                    line_height
                },
                style
            );
        }
    }

    if (IsFocused()) {
        const auto caret = CaretRect(renderer, style);
        if (caret.height > 0.0f) {
            renderer.FillRect(caret, tokens.foreground, 0.0f);
        }
    }
    RenderFocusRing(renderer, tokens, tokens.radius_medium);
}

void Textarea::Layout(const Layout::Rect& bounds) {
    SetBounds(bounds);
}

bool Textarea::HandleEvent(const Event& event) {
    if (!IsEnabled() || read_only_) {
        return false;
    }
    switch (event.type) {
        case Event::Type::Click: {
            // Approximate caret placement: click maps to nearest line/col.
            const auto bounds = GetBounds();
            const float pad = 8.0f;
            const float line_height = 14.0f * 1.4f;
            const int line = std::clamp(
                static_cast<int>((event.position.y - bounds.y - pad) / line_height),
                0, 64);
            MoveLine(line - cursor_line_);
            return true;
        }
        case Event::Type::KeyDown: {
            switch (event.key) {
                case KeyCode::Backspace: Backspace(); return true;
                case KeyCode::Delete: DeleteForward(); return true;
                case KeyCode::Enter: InsertNewline(); return true;
                case KeyCode::ArrowLeft: MoveCursor(0, -1); return true;
                case KeyCode::ArrowRight: MoveCursor(0, 1); return true;
                case KeyCode::ArrowUp: MoveLine(-1); return true;
                case KeyCode::ArrowDown: MoveLine(1); return true;
                case KeyCode::Home: MoveVisualLineStartEnd(false); return true;
                case KeyCode::End: MoveVisualLineStartEnd(true); return true;
                case KeyCode::Escape: SetFocused(false); Invalidate(); return true;
                default: return false;
            }
        }
        case Event::Type::TextInput:
            for (char c : event.text) {
                if (c == '\r' || c == '\n') {
                    InsertNewline();
                } else if (c == '\t') {
                    InsertChar(' ');
                    InsertChar(' ');
                } else if (c >= 0x20) {
                    InsertChar(c);
                }
            }
            return true;
        default:
            return false;
    }
}

void Textarea::InsertChar(char c) {
    auto pos = CaretPosition();
    // pos.second is a line index; find char offset.
    const auto lines = SplitLines(text_);
    int offset = 0;
    for (int i = 0; i < pos.first && i < static_cast<int>(lines.size()); ++i) {
        offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
    }
    offset += std::min(pos.second, static_cast<int>(lines[static_cast<std::size_t>(pos.first)].size()));
    text_.insert(static_cast<std::size_t>(offset), 1, c);
    ++cursor_col_;
    Invalidate();
    if (on_changed_) {
        on_changed_(text_);
    }
}

void Textarea::InsertNewline() {
    auto pos = CaretPosition();
    const auto lines = SplitLines(text_);
    int offset = 0;
    for (int i = 0; i < pos.first && i < static_cast<int>(lines.size()); ++i) {
        offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
    }
    offset += std::min(pos.second, static_cast<int>(lines[static_cast<std::size_t>(pos.first)].size()));
    text_.insert(static_cast<std::size_t>(offset), 1, '\n');
    ++cursor_line_;
    cursor_col_ = 0;
    Invalidate();
    if (on_changed_) {
        on_changed_(text_);
    }
}

void Textarea::Backspace() {
    if (cursor_col_ > 0) {
        auto pos = CaretPosition();
        const auto lines = SplitLines(text_);
        int offset = 0;
        for (int i = 0; i < pos.first && i < static_cast<int>(lines.size()); ++i) {
            offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
        }
        offset += cursor_col_ - 1;
        text_.erase(static_cast<std::size_t>(offset), 1);
        --cursor_col_;
    } else if (cursor_line_ > 0) {
        const auto lines = SplitLines(text_);
        const int prev_len = static_cast<int>(lines[static_cast<std::size_t>(cursor_line_ - 1)].size());
        int offset = 0;
        for (int i = 0; i < cursor_line_ && i < static_cast<int>(lines.size()); ++i) {
            offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
        }
        text_.erase(static_cast<std::size_t>(offset - 1), 1);
        --cursor_line_;
        cursor_col_ = prev_len;
    } else {
        return;
    }
    Invalidate();
    if (on_changed_) {
        on_changed_(text_);
    }
}

void Textarea::DeleteForward() {
    const auto lines = SplitLines(text_);
    if (cursor_line_ >= static_cast<int>(lines.size())) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    if (cursor_col_ < static_cast<int>(line.size())) {
        int offset = 0;
        for (int i = 0; i < cursor_line_; ++i) {
            offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
        }
        text_.erase(static_cast<std::size_t>(offset + cursor_col_), 1);
    } else if (cursor_line_ + 1 < static_cast<int>(lines.size())) {
        int offset = 0;
        for (int i = 0; i <= cursor_line_; ++i) {
            offset += static_cast<int>(lines[static_cast<std::size_t>(i)].size()) + 1;
        }
        text_.erase(static_cast<std::size_t>(offset - 1), 1);
    } else {
        return;
    }
    Invalidate();
    if (on_changed_) {
        on_changed_(text_);
    }
}

void Textarea::MoveCursor(int, int dcol) {
    const auto lines = SplitLines(text_);
    if (lines.empty()) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(std::clamp(cursor_line_, 0, static_cast<int>(lines.size()) - 1))];
    cursor_col_ = std::clamp(cursor_col_ + dcol, 0, static_cast<int>(line.size()));
    Invalidate();
}

void Textarea::MoveLine(int delta) {
    const auto lines = SplitLines(text_);
    cursor_line_ = std::clamp(cursor_line_ + delta, 0, static_cast<int>(lines.size()) - 1);
    ClampCursor();
    Invalidate();
}

void Textarea::MoveVisualLineStartEnd(bool end) {
    const auto lines = SplitLines(text_);
    if (lines.empty()) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(std::clamp(cursor_line_, 0, static_cast<int>(lines.size()) - 1))];
    cursor_col_ = end ? static_cast<int>(line.size()) : 0;
    Invalidate();
}

void Textarea::ClampCursor() {
    const auto lines = SplitLines(text_);
    cursor_line_ = std::clamp(cursor_line_, 0, std::max(0, static_cast<int>(lines.size()) - 1));
    if (!lines.empty()) {
        cursor_col_ = std::clamp(
            cursor_col_, 0,
            static_cast<int>(lines[static_cast<std::size_t>(cursor_line_)].size()));
    } else {
        cursor_col_ = 0;
    }
}

std::pair<int, int> Textarea::CaretPosition() const {
    return {cursor_line_, cursor_col_};
}

Layout::Rect Textarea::CaretRect(Rendering::Renderer& renderer, const Rendering::TextStyle& style) {
    const auto bounds = GetBounds();
    const float pad = 8.0f;
    const float line_height = 14.0f * 1.4f;
    const auto lines = SplitLines(text_);
    if (cursor_line_ < scroll_line_ || cursor_line_ >= scroll_line_ + 8) {
        scroll_line_ = std::max(0, cursor_line_ - 1);
    }
    const int visual = cursor_line_ - scroll_line_;
    std::string line_prefix;
    if (cursor_line_ < static_cast<int>(lines.size())) {
        line_prefix = lines[static_cast<std::size_t>(cursor_line_)].substr(0, static_cast<std::size_t>(cursor_col_));
    }
    const float caret_x = pad + renderer.MeasureText(line_prefix, style).width;
    const float caret_y = pad + static_cast<float>(visual) * line_height;
    return Layout::Rect{bounds.x + caret_x, bounds.y + caret_y, 1.5f, line_height};
}

Layout::Size Textarea::GetMinSize() const {
    return {0.0f, 60.0f};
}

Layout::Size Textarea::GetPreferredSize() const {
    const auto lines = SplitLines(text_);
    return {0.0f, 60.0f + static_cast<float>(lines.size()) * 19.6f};
}

Layout::Size Textarea::GetMaxSize() const {
    return {100000.0f, 100000.0f};
}

} // namespace XER::UI::Widgets
