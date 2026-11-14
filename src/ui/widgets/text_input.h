#ifndef XER_UI_WIDGETS_TEXT_INPUT_H
#define XER_UI_WIDGETS_TEXT_INPUT_H

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

class TextInput final : public Widget {
public:
    explicit TextInput(std::string value = {});

    const std::string& GetValue() const;
    void SetValue(std::string value);
    void SetOnValueChanged(std::function<void(const std::string&)> callback);
    // Hint text shown while the field is empty.
    void SetPlaceholder(std::string placeholder);
    // Renders the value as bullets (one per code point) for password
    // fields. GetValue() still returns the real text.
    void SetPasswordMode(bool enabled);
    // Called when Enter is pressed while the field has focus (single-line
    // inputs treat Enter as submit, not newline).
    void SetOnSubmit(std::function<void()> callback);

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    struct EditState {
        std::string value;
        std::size_t caret = 0;
        std::size_t anchor = 0;
    };

    void InsertText(const std::string& text);
    // Ctrl+A/C/X/V/Z/Y (Ctrl+Shift+Z = redo). Returns true when handled.
    bool HandleShortcut(KeyCode key, bool shift);
    bool HasSelection() const;
    std::pair<std::size_t, std::size_t> SelectionRange() const;
    bool DeleteSelection();
    void EraseRange(std::size_t begin, std::size_t end);
    void MoveCaret(std::size_t position, bool extend_selection);
    void PushUndo();
    bool Undo();
    bool Redo();
    void NotifyValueChanged();
    // Display text: the value itself, or one bullet per code point in
    // password mode. Byte offsets in the result do NOT match value_.
    std::string DisplayText(std::string_view value) const;
    // Maps a client-x coordinate to a caret byte offset. Needs a renderer for
    // text measurement, so it runs lazily from Render().
    std::size_t CaretIndexForX(
        float x,
        Rendering::Renderer& renderer,
        const Layout::Rect& text_bounds,
        const Rendering::TextStyle& style
    ) const;

    std::string value_;
    std::string placeholder_;
    std::function<void()> on_submit_;
    bool password_mode_ = false;
    std::size_t caret_ = 0;
    // Selection spans [min(anchor, caret), max(anchor, caret)).
    std::size_t selection_anchor_ = 0;
    std::vector<EditState> undo_stack_;
    std::vector<EditState> redo_stack_;
    // Consecutive typed characters merge into a single undo step.
    bool coalesce_typing_ = false;
    std::optional<float> pending_caret_x_;
    std::function<void(const std::string&)> on_value_changed_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TEXT_INPUT_H
