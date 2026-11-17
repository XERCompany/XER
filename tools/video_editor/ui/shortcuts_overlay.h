// Keyboard-shortcuts reference overlay (F1 / "?" toggles it from the
// app shell — the host wires the key; the overlay itself just renders
// as a dismissible card over the workspace).
//
// Styled like SettingsDialog: dimmed backdrop, centered card, accent
// edge bar. The body is a two-column grouped list — group captions
// ("Tools", "Playback", ...) over rows of [key chip] + action. Escape
// or a click on the backdrop dismisses; the list scrolls inside the
// card when it outgrows the viewport (mouse wheel).
//
// The table is display-only and mirrors the bindings in
// VideoEditorAppWidget::HandleEvent + the menu labels — keep it in
// sync when shortcuts change there.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Label;
}

namespace XER::VideoEditor::Ui {

class ShortcutsOverlay final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Optional — fired when the overlay dismisses itself (Escape,
        // backdrop click). Hosts using WidgetTree overlays can use it
        // to HideOverlay()/restore focus.
        std::function<void()> on_close;
    };

    explicit ShortcutsOverlay(Hooks hooks = {});

    // Flips visibility; opening resets the scroll to the top.
    void Toggle();
    bool IsOpen() const { return IsVisible(); }
    // Hides the card and fires hooks_.on_close (safe to call when
    // already hidden — no-op then).
    void Close();

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    // Modal overlay — fills whatever bounds the host gives it (the
    // app passes the full workspace rect, like SettingsDialog).
    XER::UI::Layout::Size GetMinSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {100000.0f, 100000.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct Entry {
        const char* keys;
        const char* action;
    };
    struct Group {
        const char* title;
        std::vector<Entry> entries;
    };

    static const std::vector<Group>& Groups();

    void EnsureChildren();
    XER::UI::Layout::Rect CardRect() const;
    // Splits Groups() into two balanced columns (indices into
    // Groups()); computed once in Layout.
    void LayoutColumns();
    float ColumnHeight(const std::vector<int>& column) const;
    float ScrollRange() const;

    Hooks hooks_;
    bool children_attached_ = false;
    float scroll_y_ = 0.0f;
    float list_top_ = 0.0f;       // layout: y where the columns begin
    float list_bottom_ = 0.0f;    // layout: columns viewport bottom
    float column_width_ = 0.0f;
    std::vector<int> column_a_;   // group indices, left column
    std::vector<int> column_b_;   // group indices, right column

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> hint_;
};

}  // namespace XER::VideoEditor::Ui
