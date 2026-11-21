// Effects panel: searchable catalog of every ClipEffect.type the
// compositor dispatches (see Compositor::Impl::ApplyClipEffects in
// tools/video_editor/compositor.cc — the `type` table is mirrored in
// kCatalog below, with each type's default parameter vector taken from
// the compositor's documented fallbacks).
//
// Typing in the search box filters by name/description. Rows are real
// Widgets::Item children inside a ScrollArea: a click selects (the
// Item's on_click), a double-click applies (on_double_click), and the
// "Add" button or Enter emits Hooks::on_add_effect with a ClipEffect
// carrying sane defaults — the app wraps the mutation in
// TimelineHistory and the inspector's effects section then exposes
// the params for editing.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Item;
class Label;
class ScrollArea;
class TextInput;
}

namespace XER::VideoEditor::Ui {

class EffectsPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void(const std::string& clip_id,
                           const ClipEffect&)> on_add_effect;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit EffectsPanelWidget(Hooks hooks = {});
    ~EffectsPanelWidget() override;

    // The clip new effects are applied to (nullptr = panel idles and
    // the Add button stays disabled).
    void SetClip(const Clip* clip);
    const std::string& ClipId() const { return clip_id_; }

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

    // One catalog entry — ClipEffect.type + the default parameter
    // list the panel's Add button emits ("script:id" carries the JS
    // fx catalog ids).
    struct EffectInfo {
        std::string type;               // ClipEffect.type ("script:id" for JS)
        std::vector<float> default_params;
        std::string description;
    };

    // The searchable built-in + JS effect catalog backing the panel —
    // also what the ve.core v3 "fx.list" service answers with (one
    // implementation, two consumers). UI thread.
    static const std::vector<EffectInfo>& Catalog();

private:

    // ScrollArea content hosting the row Items and painting the
    // selection/hover chrome — defined in the .cc (incomplete type is
    // fine: the destructor is out-of-line there).
    class RowList;

    void EnsureChildren();
    void RecomputeFilter();
    void UpdateAddEnabled();
    // Selection bookkeeping shared by row clicks (catalog index) and
    // arrow keys (filtered position).
    void SelectRow(int filtered_pos);
    void SelectCatalogRow(int catalog_index);
    // Scrolls the ScrollArea so the selected row stays in view.
    void EnsureSelectionVisible();
    void AddSelected();
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    std::string clip_id_;
    std::vector<int> filtered_;   // indices into Catalog()
    int selected_ = -1;           // index into filtered_
    bool children_attached_ = false;

    std::shared_ptr<XER::UI::Widgets::TextInput> search_;
    std::shared_ptr<XER::UI::Widgets::Button> add_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
    std::shared_ptr<XER::UI::Widgets::ScrollArea> scroll_area_;
    std::shared_ptr<RowList> list_;
    // One row Item per Catalog() entry; visibility/order comes from
    // filtered_ — rebuilt only once, never per keystroke.
    std::vector<std::shared_ptr<XER::UI::Widgets::Item>> items_;
};

}  // namespace XER::VideoEditor::Ui
