// Font Manager — modal system-font browser for the video editor.
// Searchable family list on the left; the selected family's styles
// plus a live Skia-rendered preview line on the right. "Use for
// selected text" forwards (family, weight, italic) through the
// on_apply hook — the host applies it to the selected text clip's
// style (or the open title editor).
//
// Same overlay conventions as TitleEditor/SettingsDialog: scrim,
// centered card, Escape/backdrop cancel.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "font_catalog.h"
#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Item;
class Label;
class ScrollArea;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class FontManager final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // Fired by "Use for selected text" with the chosen face.
        std::function<void(const std::string& family, int weight,
                           bool italic)>
            on_apply;
        std::function<void()> on_cancel;
    };

    explicit FontManager(Hooks hooks = {});

    // Pre-selects a face — call before showing (all args optional;
    // unmatched family falls back to the first row).
    void SetSelection(const std::string& family, int weight,
                      bool italic);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

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
    class RowList;   // ScrollArea content: stacks Item rows

    void EnsureChildren();
    void Cancel();
    void Apply();
    void RecomputeFilter();          // search -> filtered_ + rows
    void RebuildStyleRows();         // selected family -> style list
    void SelectFamily(int filtered_pos);
    void SelectStyle(int index);     // index into the family's styles
    void UpdateApplyEnabled();
    void RefreshPreview();           // (re)rasterizes the preview line
    const FontStyleInfo* CurrentStyle() const;

    XER::UI::Layout::Rect PanelRect() const;
    XER::UI::Layout::Rect PreviewRect() const;

    Hooks hooks_;
    bool children_attached_ = false;
    int selected_family_ = -1;       // index into FontCatalog::Families
    int selected_style_ = -1;        // index into family.styles
    std::vector<int> filtered_;      // display order -> family index

    // Preview cache — re-rendered only when the selection changes.
    bool preview_dirty_ = true;
    std::vector<std::uint8_t> preview_pixels_;
    int preview_w_ = 0;
    int preview_h_ = 0;
    std::string preview_label_;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::TextInput> search_;
    std::shared_ptr<RowList> family_rows_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> family_scroll_;
    std::shared_ptr<RowList> style_rows_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> style_scroll_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_button_;
    std::shared_ptr<XER::UI::Widgets::Button> cancel_button_;
};

}  // namespace XER::VideoEditor::Ui
