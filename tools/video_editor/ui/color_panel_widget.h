// Color panel: catalog of the named OCIO profiles declared in
// assets/color.yaml (color/ocio_transform.h — SharedOcioProfiles()).
// Rows are grouped into "LOOKS" (LUT-backed, lut_path non-empty) and
// "TRANSFORMS" (colorspace conversions) sections; each row shows the
// profile name plus a small subtitle — the LUT file name or the
// effective "src → display/view" chain.
//
// Rows are real Widgets::Item children inside a ScrollArea (section
// headers stay drawn, per the drawn-header convention): a click
// selects (Item on_click), a double-click or the "Add to Clip" button
// (Enter works too) emits the on_add_effect callback with the
// ClipEffect type string "ocio:<name>" — the app wraps the mutation
// in TimelineHistory and applies it to the current selection. A
// read-only header line reports whether the monitor display
// transform (OcioConfig.enabled) is on. Refresh() re-reads
// SharedOcioProfiles() after color.yaml has been (re)loaded.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "color/ocio_transform.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Item;
class Label;
class ScrollArea;
}

namespace XER::VideoEditor::Ui {

class ColorPanelWidget final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<ColorPanelWidget> Create();
    ~ColorPanelWidget() override;

    // Emitted by "Add to Clip" with the profile effect type string
    // "ocio:<name>" — the app adds a ClipEffect to the selection.
    void SetOnAddEffect(
        std::function<void(const std::string& ocio_effect_type)> cb);
    // Emitted when the selected row changes; "" when no row is
    // selected.
    void SetOnSelectionChanged(
        std::function<void(const std::string& name)> cb);

    // Re-reads SharedOcioProfiles() and rebuilds the list — call
    // after color.yaml has been (re)loaded.
    void Refresh();

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 200.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {280.0f, 420.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    ColorPanelWidget();   // use Create()

    // One line in the scrollable list — either a section header or a
    // selectable profile row.
    struct Entry {
        bool header = false;
        std::string text;   // header label or profile name
        std::string tag;    // lut file / "src → display/view" (rows)
        std::string name;   // profile name (rows only)
    };

    // ScrollArea content mixing drawn section headers with Item rows —
    // defined in the .cc (incomplete type is fine: the destructor is
    // out-of-line there).
    class RowList;

    void EnsureChildren();
    // (Re)builds the RowList lines from entries_ — Items keep their
    // entry index through a rebuild via SetOnClick's capture.
    void RebuildList();
    void UpdateAddEnabled();
    void EmitSelection();
    float EntryHeight(int index) const;
    float EntryTop(int index) const;   // content-space y of the line
    void SelectRow(int entry_index);
    void EnsureSelectionVisible();
    void AddSelected();
    void SetStatus(const std::string& text);

    std::function<void(const std::string&)> on_add_effect_;
    std::function<void(const std::string&)> on_selection_changed_;
    std::vector<Entry> entries_;    // headers interleaved with rows
    int selected_ = -1;             // index into entries_ (rows only)
    bool children_attached_ = false;
    bool ocio_enabled_ = false;     // OcioConfig.enabled at Refresh()

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> ocio_state_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_area_;
    std::shared_ptr<RowList> list_;
};

}  // namespace XER::VideoEditor::Ui
