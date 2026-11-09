#ifndef XER_UI_WIDGETS_TEXTAREA_H
#define XER_UI_WIDGETS_TEXTAREA_H

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// shadcn-style multiline text input. Supports typing, Backspace/Delete,
// Enter for newlines, arrow keys, Home/End and Ctrl+A. No scrolling — the
// widget clips rendering to its bounds.
class Textarea final : public Widget {
public:
    explicit Textarea(std::string text = {});

    void SetText(std::string text);
    const std::string& GetText() const { return text_; }
    void SetPlaceholder(std::string placeholder);
    void SetReadOnly(bool read_only);
    void SetOnChanged(std::function<void(const std::string&)> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void InsertText(const std::string& text);
    void InsertBytes(std::string_view bytes);
    void InsertNewline();
    void Backspace();
    void DeleteForward();
    void MoveCursorHorizontal(int direction);
    void MoveLine(int delta);
    void MoveVisualLineStartEnd(bool end);
    void ClampCursor();
    // Absolute byte offset of the caret inside text_.
    std::size_t CaretByteOffset() const;
    // Byte offset where `line` starts inside text_.
    std::size_t LineStartOffset(int line) const;
    // Scrolls just enough to keep the caret inside the visible band.
    void EnsureCaretVisible();
    int VisibleLineCount() const;
    // Maps a click position to (line, col); needs a renderer for text
    // measurement, so it runs lazily from Render() like TextInput.
    void ResolveClick(Layout::Point position,
                      Rendering::Renderer& renderer,
                      const Rendering::TextStyle& style);
    std::pair<int, int> CaretPosition() const;
    Layout::Rect CaretRect(Rendering::Renderer& renderer, const Rendering::TextStyle& style);
    void Changed();

    std::string text_;
    std::string placeholder_;
    int cursor_line_ = 0;
    // Byte offset within the current line (UTF-8 safe).
    int cursor_col_ = 0;
    int scroll_line_ = 0;
    bool read_only_ = false;
    std::optional<Layout::Point> pending_click_;
    // Metrics captured from the last Render() so event-time code (click
    // resolution) uses the same values that were painted.
    float pad_ = 8.0f;
    float line_height_ = 19.6f;
    std::function<void(const std::string&)> on_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TEXTAREA_H
