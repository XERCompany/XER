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

bool IsContinuation(unsigned char byte) {
    return (byte & 0xC0u) == 0x80u;
}

// Byte offset of the code point that starts at or before `position`
// (i.e. the boundary you land on when moving left).
std::size_t PrevCodePoint(std::string_view text, std::size_t position) {
    if (position == 0) {
        return 0;
    }
    --position;
    while (position > 0 && IsContinuation(static_cast<unsigned char>(text[position]))) {
        --position;
    }
    return position;
}

// Byte offset just past the code point starting at `position`.
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

// Expected UTF-8 sequence length from the lead byte (1-4).
std::size_t Utf8SeqLen(unsigned char lead) {
    if (lead < 0x80u) return 1;
    if ((lead & 0xE0u) == 0xC0u) return 2;
    if ((lead & 0xF0u) == 0xE0u) return 3;
    if ((lead & 0xF8u) == 0xF0u) return 4;
    return 1;  // stray continuation/invalid lead: treat as single byte
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
    // Read-only areas are display-only: keep them out of the tab order.
    SetFocusable(!read_only_);
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
    // Cache the metrics event-time code (click resolution, CaretRect)
    // uses, so clicks land where text was actually painted.
    pad_ = tokens.spacing_small;
    line_height_ = tokens.font_body * 1.4f;

    // Resolve a pending click before painting so the caret and the
    // scrolled line window are correct for this frame.
    if (pending_click_.has_value()) {
        ResolveClick(*pending_click_, renderer, style);
        pending_click_.reset();
    }

    const auto lines = SplitLines(text_);
    const int first_line = scroll_line_;
    const int max_lines = VisibleLineCount();

    renderer.PushClip(bounds);
    if (text_.empty() && !placeholder_.empty() && !IsFocused()) {
        renderer.DrawText(
            placeholder_,
            Layout::Rect{bounds.x + pad_, bounds.y + pad_,
                         bounds.width - 2.0f * pad_, line_height_},
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
                    bounds.x + pad_,
                    bounds.y + pad_ + static_cast<float>(i) * line_height_,
                    bounds.width - 2.0f * pad_,
                    line_height_
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
    renderer.PopClip();
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
        case Event::Type::MouseDown:
        case Event::Type::Click: {
            if (event.type == Event::Type::MouseDown &&
                event.mouse_button != MouseButton::Left) {
                return false;
            }
            // Caret placement needs text measurement — defer to Render().
            pending_click_ = event.position;
            Invalidate();
            return true;
        }
        case Event::Type::KeyDown: {
            switch (event.key) {
                case KeyCode::Backspace: Backspace(); return true;
                case KeyCode::Delete: DeleteForward(); return true;
                case KeyCode::Enter: InsertNewline(); return true;
                case KeyCode::ArrowLeft: MoveCursorHorizontal(-1); return true;
                case KeyCode::ArrowRight: MoveCursorHorizontal(1); return true;
                case KeyCode::ArrowUp: MoveLine(-1); return true;
                case KeyCode::ArrowDown: MoveLine(1); return true;
                case KeyCode::Home: MoveVisualLineStartEnd(false); return true;
                case KeyCode::End: MoveVisualLineStartEnd(true); return true;
                case KeyCode::Escape: SetFocused(false); Invalidate(); return true;
                default: return false;
            }
        }
        case Event::Type::TextInput:
            InsertText(event.text);
            return true;
        default:
            return false;
    }
}

void Textarea::InsertText(const std::string& text) {
    // Walk the UTF-8 input: control characters become editing ops, all
    // other bytes (including multi-byte sequences) are inserted verbatim
    // so non-ASCII text survives intact.
    std::size_t i = 0;
    while (i < text.size()) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '\r' || c == '\n') {
            InsertNewline();
            ++i;
            if (c == '\r' && i < text.size() && text[i] == '\n') {
                ++i;
            }
        } else if (c == '\t') {
            InsertBytes("  ");
            ++i;
        } else if (c < 0x20u) {
            ++i;  // drop other C0 controls
        } else {
            const std::size_t take =
                std::min(Utf8SeqLen(c), text.size() - i);
            InsertBytes(std::string_view(text).substr(i, take));
            i += take;
        }
    }
}

void Textarea::InsertBytes(std::string_view bytes) {
    if (bytes.empty()) {
        return;
    }
    text_.insert(CaretByteOffset(), bytes);
    cursor_col_ += static_cast<int>(bytes.size());
    Changed();
}

void Textarea::InsertNewline() {
    const auto lines = SplitLines(text_);
    const int col = std::min(
        cursor_col_,
        cursor_line_ < static_cast<int>(lines.size())
            ? static_cast<int>(lines[static_cast<std::size_t>(cursor_line_)].size())
            : 0);
    text_.insert(LineStartOffset(cursor_line_) + static_cast<std::size_t>(col), 1, '\n');
    ++cursor_line_;
    cursor_col_ = 0;
    Changed();
}

void Textarea::Backspace() {
    const auto lines = SplitLines(text_);
    const int line_count = static_cast<int>(lines.size());
    if (cursor_line_ >= line_count) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    const int col = std::min(cursor_col_, static_cast<int>(line.size()));
    if (col > 0) {
        // Erase the whole previous code point, not just one byte —
        // single-byte erases would split UTF-8 sequences.
        const auto prev = PrevCodePoint(line, static_cast<std::size_t>(col));
        text_.erase(LineStartOffset(cursor_line_) + prev,
                    static_cast<std::size_t>(col) - prev);
        cursor_col_ = static_cast<int>(prev);
    } else if (cursor_line_ > 0) {
        const int prev_len = static_cast<int>(
            lines[static_cast<std::size_t>(cursor_line_ - 1)].size());
        text_.erase(LineStartOffset(cursor_line_) - 1, 1);
        --cursor_line_;
        cursor_col_ = prev_len;
    } else {
        return;
    }
    Changed();
}

void Textarea::DeleteForward() {
    const auto lines = SplitLines(text_);
    const int line_count = static_cast<int>(lines.size());
    if (cursor_line_ >= line_count) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    const int col = std::min(cursor_col_, static_cast<int>(line.size()));
    if (col < static_cast<int>(line.size())) {
        const auto next = NextCodePoint(line, static_cast<std::size_t>(col));
        text_.erase(LineStartOffset(cursor_line_) + static_cast<std::size_t>(col),
                    next - static_cast<std::size_t>(col));
    } else if (cursor_line_ + 1 < line_count) {
        text_.erase(LineStartOffset(cursor_line_) + line.size(), 1);
    } else {
        return;
    }
    Changed();
}

void Textarea::MoveCursorHorizontal(int direction) {
    const auto lines = SplitLines(text_);
    const int line_count = static_cast<int>(lines.size());
    if (cursor_line_ >= line_count) {
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    const int col = std::min(cursor_col_, static_cast<int>(line.size()));
    if (direction < 0) {
        cursor_col_ = static_cast<int>(PrevCodePoint(line, static_cast<std::size_t>(col)));
    } else {
        cursor_col_ = static_cast<int>(NextCodePoint(line, static_cast<std::size_t>(col)));
    }
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
    if (lines.empty()) {
        cursor_col_ = 0;
        return;
    }
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    int col = std::clamp(cursor_col_, 0, static_cast<int>(line.size()));
    // A byte column carried over from a different line (arrow up/down,
    // SetText) can land inside a multi-byte UTF-8 sequence. Snap back to
    // the sequence start so edits never split a code point.
    if (col > 0 && col < static_cast<int>(line.size()) &&
        IsContinuation(static_cast<unsigned char>(line[static_cast<std::size_t>(col)]))) {
        col = static_cast<int>(PrevCodePoint(line, static_cast<std::size_t>(col)));
    }
    cursor_col_ = col;
}

std::pair<int, int> Textarea::CaretPosition() const {
    return {cursor_line_, cursor_col_};
}

std::size_t Textarea::LineStartOffset(int line) const {
    const auto lines = SplitLines(text_);
    std::size_t offset = 0;
    const int count = std::min(line, static_cast<int>(lines.size()));
    for (int i = 0; i < count; ++i) {
        offset += lines[static_cast<std::size_t>(i)].size() + 1;
    }
    return offset;
}

std::size_t Textarea::CaretByteOffset() const {
    const auto lines = SplitLines(text_);
    if (cursor_line_ >= static_cast<int>(lines.size())) {
        return text_.size();
    }
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    const int col = std::min(cursor_col_, static_cast<int>(line.size()));
    return LineStartOffset(cursor_line_) + static_cast<std::size_t>(col);
}

int Textarea::VisibleLineCount() const {
    const auto bounds = GetBounds();
    if (bounds.height <= 0.0f || line_height_ <= 0.0f) {
        return 1;
    }
    return std::max(1, static_cast<int>(bounds.height / line_height_) - 1);
}

void Textarea::EnsureCaretVisible() {
    const int visible = VisibleLineCount();
    if (cursor_line_ < scroll_line_) {
        scroll_line_ = cursor_line_;
    } else if (cursor_line_ >= scroll_line_ + visible) {
        scroll_line_ = cursor_line_ - visible + 1;
    }
    const auto lines = SplitLines(text_);
    scroll_line_ = std::clamp(scroll_line_, 0,
                              std::max(0, static_cast<int>(lines.size()) - 1));
}

void Textarea::ResolveClick(Layout::Point position,
                            Rendering::Renderer& renderer,
                            const Rendering::TextStyle& style) {
    const auto bounds = GetBounds();
    const auto lines = SplitLines(text_);
    const int line_count = static_cast<int>(lines.size());

    const int row = std::clamp(
        static_cast<int>((position.y - bounds.y - pad_) / line_height_),
        0, VisibleLineCount() - 1);
    cursor_line_ = std::clamp(scroll_line_ + row, 0, line_count - 1);

    // Column from x: first code-point boundary whose prefix width
    // exceeds the click offset.
    const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
    const float local_x = position.x - bounds.x - pad_;
    if (local_x <= 0.0f) {
        cursor_col_ = 0;
    } else {
        cursor_col_ = static_cast<int>(line.size());
        for (std::size_t p = 0; p <= line.size();) {
            const float width = renderer.MeasureText(
                std::string_view(line).substr(0, p), style).width;
            if (width > local_x) {
                cursor_col_ = static_cast<int>(
                    p == 0 ? 0 : PrevCodePoint(line, p));
                break;
            }
            if (p == line.size()) {
                break;
            }
            p = NextCodePoint(line, p);
        }
    }
    EnsureCaretVisible();
}

Layout::Rect Textarea::CaretRect(Rendering::Renderer& renderer, const Rendering::TextStyle& style) {
    const auto bounds = GetBounds();
    EnsureCaretVisible();
    const int visual = cursor_line_ - scroll_line_;
    const auto lines = SplitLines(text_);
    std::string line_prefix;
    if (cursor_line_ < static_cast<int>(lines.size())) {
        const auto& line = lines[static_cast<std::size_t>(cursor_line_)];
        line_prefix = line.substr(
            0, static_cast<std::size_t>(
                   std::min(cursor_col_, static_cast<int>(line.size()))));
    }
    const float caret_x = pad_ + renderer.MeasureText(line_prefix, style).width;
    const float caret_y = pad_ + static_cast<float>(visual) * line_height_;
    return Layout::Rect{bounds.x + caret_x, bounds.y + caret_y, 1.5f, line_height_};
}

void Textarea::Changed() {
    ClampCursor();
    EnsureCaretVisible();
    Invalidate();
    if (on_changed_) {
        on_changed_(text_);
    }
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
