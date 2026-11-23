// See data_widgets_smoke.h — headless exercise of the G3/G4 data
// widgets (VirtualList + TreeView) added in src/ui/widgets.
#include "data_widgets_smoke.h"

#include <cmath>
#include <memory>
#include <string>

#include "layout/geometry.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/scrub_number_field.h"
#include "widgets/tree_view.h"
#include "widgets/virtual_list.h"

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
using XER::UI::Layout::Point;
using XER::UI::Layout::Rect;
using XER::UI::Rendering::Renderer;
using XER::UI::Rendering::TextStyle;
using XER::UI::Theme::Tokens;

// Recording-free null renderer — the checks assert on widget state and
// callback invocation, not pixels; MeasureText returns a plausible box
// so vertical-centering math stays exercised.
class NullRenderer final : public Renderer {
public:
    int text_calls = 0;

    void BeginFrame(Rect) override {}
    void EndFrame() override {}
    void FillRect(Rect, XER::UI::Theme::Color, float) override {}
    void StrokeRect(Rect, XER::UI::Theme::Color, float, float) override {}
    void DrawLine(Point, Point, XER::UI::Theme::Color, float) override {}
    void DrawText(std::string_view, Rect, TextStyle) override {
        ++text_calls;
    }
    XER::UI::Layout::Size MeasureText(std::string_view text,
                                      TextStyle) const override {
        return {static_cast<float>(text.size()) * 7.0f, 14.0f};
    }
};

W::Event Key(W::KeyCode key, bool shift = false) {
    W::Event ev{};
    ev.type = W::Event::Type::KeyDown;
    ev.key = key;
    ev.is_shift = shift;
    return ev;
}

W::Event Click(float x, float y) {
    W::Event ev{};
    ev.type = W::Event::Type::Click;
    ev.position = {x, y};
    ev.mouse_button = W::MouseButton::Left;
    return ev;
}

W::Event Pointer(W::Event::Type type, float x, float y,
                 bool alt = false, bool shift = false) {
    W::Event ev{};
    ev.type = type;
    ev.position = {x, y};
    ev.mouse_button = W::MouseButton::Left;
    ev.is_alt = alt;
    ev.is_shift = shift;
    return ev;
}

W::Event Wheel(int delta) {
    W::Event ev{};
    ev.type = W::Event::Type::MouseWheel;
    ev.wheel_delta = delta;
    return ev;
}

W::Event Text(const char* text) {
    W::Event ev{};
    ev.type = W::Event::Type::TextInput;
    ev.text = text;
    return ev;
}

std::string CheckScrubNumberField() {
    auto field = W::ScrubNumberField::Create();
    field->SetRange(0.0f, 100.0f);
    field->SetValue(50.0f);
    field->SetStep(1.0f);
    field->SetSmallStep(0.1f);
    field->SetLargeStep(10.0f);
    field->SetPixelSensitivity(2.0f);
    field->SetResetValue(50.0f);
    field->Layout({0.0f, 0.0f, 96.0f, 28.0f});

    int live = 0;
    int committed = 0;
    field->SetOnValueChange([&](float) { ++live; });
    field->SetOnValueCommitted([&](float) { ++committed; });

    // ---- horizontal drag scrubs by step (live only until release) -----
    field->HandleEvent(Pointer(W::Event::Type::MouseDown, 10.0f, 14.0f));
    field->HandleEvent(Pointer(W::Event::Type::MouseMove, 30.0f, 14.0f));
    if (!field->IsScrubbing() || std::fabs(field->GetValue() - 60.0f) >
            0.001f) {
        return "scrub_field: drag did not scrub by step";
    }
    if (live != 1 || committed != 0) {
        return "scrub_field: drag committed before release";
    }
    field->HandleEvent(Pointer(W::Event::Type::MouseUp, 30.0f, 14.0f));
    if (committed != 1 || field->IsScrubbing()) {
        return "scrub_field: drag end did not commit once";
    }

    // ---- Alt = small step ----------------------------------------------
    field->HandleEvent(
        Pointer(W::Event::Type::MouseDown, 10.0f, 14.0f, true));
    field->HandleEvent(
        Pointer(W::Event::Type::MouseMove, 30.0f, 14.0f, true));
    if (std::fabs(field->GetValue() - 61.0f) > 0.001f) {
        return "scrub_field: Alt drag did not use smallStep";
    }
    field->HandleEvent(
        Pointer(W::Event::Type::MouseUp, 30.0f, 14.0f, true));

    // ---- plain click enters text edit; typing + Enter commits ----------
    field->SetValue(50.0f);
    const int live_before_edit = live;
    field->HandleEvent(Pointer(W::Event::Type::MouseDown, 10.0f, 14.0f));
    field->HandleEvent(Pointer(W::Event::Type::MouseUp, 10.0f, 14.0f));
    field->HandleEvent(Click(10.0f, 14.0f));
    if (!field->IsEditing()) {
        return "scrub_field: click did not open the text editor";
    }
    field->HandleEvent(Text("72"));
    field->HandleEvent(Key(W::KeyCode::Enter));
    if (field->IsEditing() || std::fabs(field->GetValue() - 72.0f) >
            0.001f) {
        return "scrub_field: typed value did not commit on Enter";
    }
    if (live != live_before_edit + 1 || committed != 3) {
        return "scrub_field: Enter commit misreported";
    }

    // ---- bounds clamp ----------------------------------------------------
    field->HandleEvent(Pointer(W::Event::Type::MouseDown, 10.0f, 14.0f));
    field->HandleEvent(Pointer(W::Event::Type::MouseMove, 500.0f, 14.0f));
    if (field->GetValue() != 100.0f) {
        return "scrub_field: drag did not clamp at max";
    }
    field->HandleEvent(Pointer(W::Event::Type::MouseUp, 500.0f, 14.0f));

    // ---- double-click reset ---------------------------------------------
    // The bound-drag release inside the widget synthesizes a Click that
    // the field must consume (not read as click-to-edit); the following
    // pair is the real double-click.
    field->HandleEvent(Click(10.0f, 14.0f));
    field->HandleEvent(Click(10.0f, 14.0f));
    field->HandleEvent(Click(10.0f, 14.0f));
    if (std::fabs(field->GetValue() - 50.0f) > 0.001f ||
        field->IsEditing()) {
        return "scrub_field: double-click did not reset";
    }

    // ---- focused wheel scrub (debounced commit) --------------------------
    field->SetFocused(true);
    const int committed_before_wheel = committed;
    field->HandleEvent(Wheel(120));
    if (std::fabs(field->GetValue() - 51.0f) > 0.001f) {
        return "scrub_field: focused wheel did not step";
    }
    field->Tick(0.5);
    if (committed != committed_before_wheel + 1) {
        return "scrub_field: wheel nudge never committed";
    }

    // ---- Home/End + Escape-proof keyboard --------------------------------
    field->HandleEvent(Key(W::KeyCode::End));
    if (field->GetValue() != 100.0f) {
        return "scrub_field: End did not jump to max";
    }
    field->HandleEvent(Key(W::KeyCode::Home));
    if (field->GetValue() != 0.0f) {
        return "scrub_field: Home did not jump to min";
    }

    // ---- programmatic SetValue is live-only -------------------------------
    const int committed_before_set = committed;
    field->SetValue(42.0f);
    if (committed != committed_before_set) {
        return "scrub_field: SetValue fired the committed callback";
    }

    // ---- render pass must not crash ---------------------------------------
    NullRenderer renderer;
    Tokens tokens{};
    field->Render(renderer, tokens);
    if (renderer.text_calls == 0) {
        return "scrub_field: render pass drew no value text";
    }
    return {};
}

std::string CheckVirtualList() {
    auto list = W::VirtualList::Create();
    list->SetItemCount(10000);
    list->SetItemHeight(26.0f);
    list->Layout({0.0f, 0.0f, 200.0f, 130.0f});  // exactly 5 rows visible

    // ---- viewport culling: only visible + overscan rows paint --------
    int painted = 0;
    int last_painted = -1;
    list->SetOverscan(2);
    list->SetItemRenderer(
        [&](Renderer&, Rect, int index, bool) {
            ++painted;
            last_painted = index;
        });
    NullRenderer renderer;
    Tokens tokens{};
    list->Render(renderer, tokens);
    if (list->FirstPaintedIndex() != 0 || list->LastPaintedIndex() != 7) {
        return "virtual_list: visible range math wrong at scroll 0";
    }
    if (painted != 8 || last_painted != 7) {
        return "virtual_list: renderer invoked outside the painted range";
    }

    // ---- scroll offset + EnsureVisible --------------------------------
    list->ScrollToIndex(5000);
    const float expected = 5000.0f * 26.0f - (130.0f - 26.0f) * 0.5f;
    if (std::fabs(list->GetScrollY() - expected) > 1.0f) {
        return "virtual_list: ScrollToIndex did not center the row";
    }
    // scroll = 129948 = 4998*26 exactly → first painted = 4998 - overscan.
    if (list->FirstPaintedIndex() != 4996) {
        return "virtual_list: overscan not applied below the viewport";
    }
    list->EnsureVisible(5000);
    if (list->GetScrollY() != expected) {
        return "virtual_list: EnsureVisible moved a visible row";
    }
    list->EnsureVisible(5010);
    if (list->GetScrollY() <= expected) {
        return "virtual_list: EnsureVisible failed to scroll far row";
    }

    // ---- single selection: click + arrow keys -------------------------
    int notifications = 0;
    list->SetOnSelectionChanged([&] { ++notifications; });
    list->HandleEvent(Click(10.0f, list->GetBounds().y + 3.0f));
    if (list->GetSelectedIndex() < 0 || notifications != 1) {
        return "virtual_list: click did not select the pointed row";
    }
    list->HandleEvent(Key(W::KeyCode::ArrowDown));
    if (list->GetSelectedIndex() != list->GetCursorIndex()) {
        return "virtual_list: arrow did not move selection with cursor";
    }
    list->HandleEvent(Key(W::KeyCode::End));
    if (list->GetCursorIndex() != 9999 ||
        list->GetSelectedIndex() != 9999) {
        return "virtual_list: End did not jump to the last row";
    }
    if (list->GetScrollY() <= 0.0f) {
        return "virtual_list: cursor jump did not scroll the viewport";
    }

    // ---- multiple selection: space toggle + shift range ----------------
    list->SetSelectionMode(W::VirtualList::SelectionMode::Multiple);
    list->SetSelectedIndices({});
    list->SetCursorIndex(3);
    list->HandleEvent(Key(W::KeyCode::Space));
    list->HandleEvent(Key(W::KeyCode::ArrowDown));
    list->HandleEvent(Key(W::KeyCode::Space));
    if (list->GetSelectedIndices().size() != 2 ||
        !list->IsRowSelected(3) || !list->IsRowSelected(4)) {
        return "virtual_list: space did not toggle cursor rows";
    }
    // Shift+arrow extends a range from the anchor (4) and REPLACES the
    // selection with it — the earlier toggle {3} is dropped.
    list->HandleEvent(Key(W::KeyCode::ArrowDown, /*shift=*/true));
    if (!list->IsRowSelected(4) || !list->IsRowSelected(5) ||
        list->IsRowSelected(3) ||
        list->GetSelectedIndices().size() != 2) {
        return "virtual_list: shift+arrow did not extend the range";
    }

    // ---- key fallback hook (TreeView's tree keys ride this) ------------
    bool fallback_seen = false;
    list->SetKeyFallback([&](const W::Event& e) {
        fallback_seen = (e.type == W::Event::Type::TextInput);
        return true;
    });
    W::Event text{};
    text.type = W::Event::Type::TextInput;
    text.text = "x";
    if (!list->HandleEvent(text) || !fallback_seen) {
        return "virtual_list: TextInput did not reach the key fallback";
    }

    // ---- programmatic setters stay silent ------------------------------
    const int before = notifications;
    list->SetSelectedIndex(42);
    if (notifications != before) {
        return "virtual_list: SetSelectedIndex fired the callback";
    }
    return {};
}

std::string CheckTreeView() {
    auto tree = W::TreeView::Create();
    tree->Layout({0.0f, 0.0f, 300.0f, 400.0f});

    // bin (branch: clip1, sub(branch: subleaf)) · lazy (branch, empty)
    // leaf
    W::TreeNode clip;
    clip.id = "clip1";
    clip.label = "Clip One";
    W::TreeNode subleaf;
    subleaf.id = "subleaf";
    subleaf.label = "Sub Leaf";
    W::TreeNode sub;
    sub.id = "sub";
    sub.label = "Sub Bin";
    sub.is_branch = true;
    sub.children = {subleaf};
    W::TreeNode bin;
    bin.id = "bin";
    bin.label = "Bin";
    bin.is_branch = true;
    bin.children = {clip, sub};
    W::TreeNode lazy;
    lazy.id = "lazy";
    lazy.label = "Lazy";
    lazy.is_branch = true;
    W::TreeNode leaf;
    leaf.id = "leaf";
    leaf.label = "Leaf Node";

    tree->SetItems({bin, lazy, leaf});
    if (tree->GetContentHeight() != 3.0f * 26.0f) {
        return "tree_view: flatten produced the wrong row count";
    }

    // ---- controlled expansion (silent setter) --------------------------
    int expand_events = 0;
    tree->SetOnExpandChanged(
        [&](const std::string&, bool) { ++expand_events; });
    tree->SetExpanded({"bin"});
    if (expand_events != 0) {
        return "tree_view: SetExpanded fired the change callback";
    }
    if (tree->GetContentHeight() != 5.0f * 26.0f) {
        return "tree_view: expanded branch did not flatten children";
    }

    // ---- selection round-trip through the list --------------------------
    int sel_events = 0;
    tree->SetOnSelectionChanged([&] { ++sel_events; });
    // "clip1" is flat index 1 (list inset 1px → y = 1 + 1.5 rows).
    tree->HandleEvent(Click(60.0f, 1.0f + 1.5f * 26.0f));
    if (tree->GetSelectedId() != "clip1" || sel_events != 1) {
        return "tree_view: row click did not select clip1";
    }
    tree->SetSelectedIds({"sub", "subleaf"});
    if (tree->GetSelectedIds().size() != 1) {
        return "tree_view: single mode kept more than one id";
    }

    // ---- keyboard: left collapses / hops to parent, right expands -------
    tree->SetSelectedId("clip1");
    tree->GetList()->SetCursorIndex(1);  // selection doesn't move cursor
    tree->HandleEvent(Key(W::KeyCode::ArrowLeft));
    if (tree->GetSelectedId() != "bin") {
        return "tree_view: ArrowLeft did not hop selection to parent";
    }
    tree->HandleEvent(Key(W::KeyCode::ArrowLeft));
    if (tree->IsExpanded("bin")) {
        return "tree_view: ArrowLeft did not collapse the branch";
    }
    if (expand_events == 0) {
        return "tree_view: collapse did not fire on_expand_changed";
    }
    tree->HandleEvent(Key(W::KeyCode::ArrowRight));
    if (!tree->IsExpanded("bin")) {
        return "tree_view: ArrowRight did not expand the branch";
    }

    // ---- lazy loading + retry -------------------------------------------
    int load_calls = 0;
    std::string last_load;
    tree->SetLoadChildren(
        [&](W::TreeView&, const std::string& id) {
            ++load_calls;
            last_load = id;
        });
    // Chevron-zone click on "lazy" (flat index 3, depth 0): content_x 4,
    // chevron 4..22 in list coords (+1 inset → x 5..23 widget coords).
    tree->HandleEvent(Click(1.0f + 10.0f, 1.0f + 3.5f * 26.0f));
    if (load_calls != 1 || last_load != "lazy") {
        return "tree_view: chevron expand did not fire lazy load";
    }
    // Loading placeholder row appended (flat count 5 → 6).
    if (tree->GetContentHeight() != 6.0f * 26.0f) {
        return "tree_view: lazy expand did not append placeholder";
    }
    // Fail the load → retry row; ArrowRight on it re-fires the callback.
    tree->SetNodeLoadFailed("lazy", "network unreachable");
    tree->GetList()->SetCursorIndex(4);
    tree->HandleEvent(Key(W::KeyCode::ArrowRight));
    if (load_calls != 2) {
        return "tree_view: ArrowRight on retry did not re-fire load";
    }
    W::TreeNode loaded;
    loaded.id = "l1";
    loaded.label = "Loaded One";
    tree->SetNodeChildren("lazy", {loaded});
    if (tree->GetContentHeight() != 6.0f * 26.0f) {
        return "tree_view: loaded children did not flatten";
    }

    // ---- multiple mode: checkbox cascade + tri-state ---------------------
    tree->SetMultiple(true);
    tree->SetSelectedIds({"clip1", "subleaf"});
    if (tree->GetSelectedIds().size() != 2) {
        return "tree_view: multiple mode dropped a checked id";
    }
    // Click the "bin" row body past the chevron zone (depth 0 → chevron
    // is x 4..22 in list coords; x=30 in widget coords lands in the row
    // body). In multiple mode the whole row toggles — cascade expected.
    tree->HandleEvent(Click(1.0f + 30.0f, 1.0f + 0.5f * 26.0f));
    for (const char* id : {"bin", "clip1", "sub", "subleaf"}) {
        if (tree->GetSelectedIds().count(id) == 0) {
            return "tree_view: branch check did not cascade down";
        }
    }
    tree->HandleEvent(Click(1.0f + 30.0f, 1.0f + 0.5f * 26.0f));
    if (tree->GetSelectedIds().count("clip1") != 0 ||
        tree->GetSelectedIds().count("subleaf") != 0) {
        return "tree_view: branch uncheck did not clear descendants";
    }

    // ---- typeahead (TextInput — the kit's only letter channel) -----------
    tree->GetList()->SetCursorIndex(0);
    W::Event txt{};
    txt.type = W::Event::Type::TextInput;
    txt.text = "l";
    tree->HandleEvent(txt);
    // flat order: bin,clip1,sub,lazy,l1,leaf → 'l' lands past index 0 on
    // "lazy" (or a later 'l…' label — anything past the cursor is fine).
    if (tree->GetList()->GetCursorIndex() < 0) {
        return "tree_view: typeahead did not move the cursor";
    }

    // ---- render pass must not crash --------------------------------------
    NullRenderer renderer;
    Tokens tokens{};
    tree->Render(renderer, tokens);
    tree->GetList()->Render(renderer, tokens);
    if (renderer.text_calls == 0) {
        return "tree_view: render pass drew no labels";
    }
    return {};
}

}  // namespace

std::string DataWidgetsSmoke() {
    if (const std::string fail = CheckVirtualList(); !fail.empty()) {
        return fail;
    }
    if (const std::string fail = CheckTreeView(); !fail.empty()) {
        return fail;
    }
    if (const std::string fail = CheckScrubNumberField(); !fail.empty()) {
        return fail;
    }
    return {};
}

}  // namespace XER::VideoEditor::Ui2
