// Color panel: a Lumetri-style "Correction" section on top of the
// OCIO profile catalog declared in assets/color.yaml
// (color/ocio_transform.h — SharedOcioProfiles()).
//
//   ┌─────────────────────────────┐
//   │ ▾ Correction                │  collapsible section header
//   │        (color wheel)        │  hue ring / saturation radius,
//   │  Temperature ────── +12.0   │  draggable indicator
//   │  Tint        ──────  -4.0   │  slider rows, mono readouts
//   │ ▾ Looks                     │  collapsible list sections:
//   │   [row][row]                │  LUT-backed profiles
//   │ ▾ Transforms                │  colorspace conversions
//   │ [Add to Clip]  status       │
//   └─────────────────────────────┘
//
// Correction controls emit fully-parameterized ClipEffects through
// SetOnCorrection when a gesture commits (wheel release / slider
// release or click): the wheel maps onto the built-in
// "color_balance" lift triplet (picked hue, radius = strength), the
// sliders onto "plugin:xer.fx.temperature" / "plugin:xer.fx.tint"
// (param0 -100..100). The host decides replace-vs-add semantics —
// like every other panel path the mutation goes through
// TimelineHistory.
//
// The catalog keeps its original wiring: rows are real Widgets::Item
// children inside a ScrollArea under collapsible section headers — a
// click selects (Item on_click), a double-click or the "Add to Clip"
// button (Enter works too) emits on_add_effect with the ClipEffect
// type string "ocio:<name>". The "Display Transform" readout moved
// onto the Correction header row. Refresh() re-reads
// SharedOcioProfiles() after color.yaml has been (re)loaded.
#pragma once

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "color/ocio_transform.h"
#include "layout/geometry.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Item;
class Label;
class ScrollArea;
class Slider;
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
    // Emitted when a Correction control commits a ready-to-apply
    // ClipEffect (wheel -> "color_balance", sliders -> the plugin fx
    // types). The host applies it to the current selection.
    void SetOnCorrection(std::function<void(const ClipEffect&)> cb);
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
        return {280.0f, 480.0f};
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
    // entry index through a rebuild via SetOnClick's capture. Rows of
    // collapsed groups are skipped.
    void RebuildList();
    void UpdateAddEnabled();
    void EmitSelection();
    float EntryHeight(int index) const;
    float EntryTop(int index) const;   // content-space y of the line
    void SelectRow(int entry_index);
    void EnsureSelectionVisible();
    void AddSelected();
    void SetStatus(const std::string& text);

    // ---- correction section ----
    // True for catalog rows inside a collapsed group (arrow-key
    // navigation skips them).
    bool EntryHidden(int index) const;
    // Wheel geometry: the square the disc rasterizes into, and the
    // disc's own center/radius.
    XER::UI::Layout::Rect WheelRect() const;
    void UpdateWheelFromPoint(const XER::UI::Layout::Point& position);
    // Lazily rasterized hue/saturation disc (premultiplied BGRA).
    void EnsureWheelBitmap();
    // The ClipEffect the current wheel position describes —
    // "color_balance" lift toward the picked hue.
    ClipEffect WheelEffect() const;
    // Commits a slider's value as a single-param ClipEffect.
    void CommitSliderEffect(const std::string& type, float value);
    void EmitCorrection(const ClipEffect& effect);

    std::function<void(const std::string&)> on_add_effect_;
    std::function<void(const ClipEffect&)> on_correction_;
    std::function<void(const std::string&)> on_selection_changed_;
    std::vector<Entry> entries_;    // headers interleaved with rows
    int selected_ = -1;             // index into entries_ (rows only)
    bool children_attached_ = false;
    bool ocio_enabled_ = false;     // OcioConfig.enabled at Refresh()
    // Catalog group labels ("LOOKS"/"TRANSFORMS") whose rows are
    // currently folded away — survives Refresh().
    std::set<std::string> collapsed_groups_;

    // Correction section state: the wheel keeps hue degrees + radius
    // fraction; sliders keep their own values.
    bool correction_collapsed_ = false;
    float wheel_hue_deg_ = 0.0f;
    float wheel_amount_ = 0.0f;     // 0..1 — disc radius fraction
    bool wheel_drag_ = false;
    std::vector<std::uint8_t> wheel_bmp_;
    // Slider gesture tracking: a drag commits once on release (Tick
    // polls IsDragging); a click/arrow-key commit fires immediately
    // from OnValueChanged instead.
    bool temp_pending_commit_ = false;
    bool tint_pending_commit_ = false;

    XER::UI::Layout::Rect header_rect_{};   // "Correction" header row
    XER::UI::Layout::Rect wheel_rect_{};    // set during Layout

    std::shared_ptr<XER::UI::Widgets::Label> ocio_state_;
    std::shared_ptr<XER::UI::Widgets::Slider> temp_slider_;
    std::shared_ptr<XER::UI::Widgets::Slider> tint_slider_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_area_;
    std::shared_ptr<RowList> list_;
};

}  // namespace XER::VideoEditor::Ui
