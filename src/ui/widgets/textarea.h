#ifndef XER_UI_WIDGETS_TEXTAREA_H
#define XER_UI_WIDGETS_TEXTAREA_H

#include <functional>
#include <string>
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
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    void InsertChar(char c);
    void InsertNewline();
    void Backspace();
    void DeleteForward();
    void MoveCursor(int dline, int dcol);
    void MoveLine(int delta);
    void MoveVisualLineStartEnd(bool end);
    void ClampCursor();
    std::pair<int, int> CaretPosition() const;
    Layout::Rect CaretRect(Rendering::Renderer& renderer, const Rendering::TextStyle& style);

    std::string text_;
    std::string placeholder_;
    int cursor_line_ = 0;
    int cursor_col_ = 0;
    int scroll_line_ = 0;
    bool read_only_ = false;
    std::function<void(const std::string&)> on_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TEXTAREA_H
