// EffectsPanel — the M3 effects browser of the ui2 rebuild (spec §6
// "Effects" dock tab): a search TextInput above a TreeView of
// category folders -> effect items, with a favorites group, kind
// badges (video / audio / track-driven) and an apply-to-selection
// hook the shell routes through TimelineHistory.
//
// The catalog is assembled by BuildFxRegistryEntries() — the live
// builtin-fx registry (builtin_fx.cc, ~51 entries incl. the
// track-driven smart_reframe / track_mosaic kernels), third-party
// plugin DLL descriptors (plugin_host.cc) and the compositor's named
// core-effect table (the compositor.cc dispatch list the legacy
// effects panel also shipped). The panel itself is a pure view:
// applying an effect emits on_apply_fx(clip_ids, fx_type) — mutation
// and undo-grouping belong to the shell.
//
// Double-click apply is implemented by wrapping the tree's internal
// VirtualList item-click hook (TreeView exposes no leaf-activation
// API); the wrapper replicates the tree's chevron-zone handling so
// branch rows keep working. A true drag-onto-timeline is NOT possible
// through TreeView rows (kit gap: no drag events on consumed clicks)
// — the apply affordances are double-click / Apply button / context
// menu for wave-1.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class TextInput;
class TreeView;
struct TreeNode;
}

namespace XER::VideoEditor::Ui2 {

// One browsable effect. `type` is the ClipEffect::type string written
// onto the clip (bare core name / "builtin:<id>" / "plugin:<id>" /
// "script:<id>").
struct FxEntry {
    enum class Badge : int {
        Video = 0,   // video effect (frame kernels)
        Audio = 1,   // audio effect (PCM kernels)
        Track = 2,   // track-driven video effect — smart_reframe /
                     // track_mosaic consume the clip's object track
    };
    std::string type;
    std::string name;          // display name
    std::string description;   // param summary / registry description
    std::string category;      // folder the browser groups it under
    std::vector<float> default_params;
    Badge badge = Badge::Video;
};

// Assembles the full effects catalog the panel browses: the builtin
// registry (BuiltinFx::Effects — every xer.fx.*/xer.af.* descriptor
// with its real param defaults), third-party plugin descriptors and
// the compositor's named core-effect table. Exposed at namespace
// scope so the shell and the ui2_panels smoke share the same source
// of truth.
std::vector<FxEntry> BuildFxRegistryEntries();

class EffectsPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<EffectsPanel> Create();

    // Apply-to-selection hook: fires with the shell's selected clip
    // ids (from SetClipSelectionProvider) and the effect's
    // ClipEffect::type. The shell appends ClipEffect{type,
    // default_params} through TimelineHistory.
    void SetOnApplyFx(
        std::function<void(const std::vector<std::string>& clip_ids,
                           const std::string& fx_type)> cb);
    // The shell supplies the timeline-selected clip ids (the effects
    // apply target — old inspector parity: applies to every selected
    // clip the kind allows).
    void SetClipSelectionProvider(
        std::function<std::vector<std::string>()> provider);
    void SetOnStatus(
        std::function<void(const std::string&, bool is_error)> cb);

    // ---- model ------------------------------------------------------
    void SetEntries(std::vector<FxEntry> entries);
    const std::vector<FxEntry>& Entries() const { return entries_; }
    const FxEntry* FindEntry(const std::string& type) const;
    // Default params for an applied effect (the ClipEffect payload).
    std::vector<float> DefaultParams(const std::string& type) const;

    // ---- search / favorites ------------------------------------------
    void SetSearchText(std::string text);
    const std::string& SearchText() const { return search_; }
    // Leaf nodes currently shown under the filter (smoke asserts).
    int VisibleEntryCount() const;
    const std::set<std::string>& Favorites() const { return favorites_; }
    void SetFavorites(std::set<std::string> types);
    bool IsFavorite(const std::string& type) const;

    // ---- apply affordances ---------------------------------------------
    std::string SelectedType() const;
    // The Apply button / double-click / menu "Apply" all funnel here —
    // fires on_apply_fx with the provider's clip ids.
    void ApplySelected();

    // Tree-level context-menu seam (unconsumed right-clicks bubble to
    // the shell's handler, which forwards here).
    bool ShowContextMenuAt(XER::UI::Layout::Point point);

    XER::UI::Widgets::TreeView* Tree() const { return tree_.get(); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {340.0f, 480.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit EffectsPanel();

    void EnsureChildren();
    void RebuildTree();
    void SyncApplyState();
    const FxEntry* EntryForTreeId(const std::string& id) const;
    // Replicates TreeView's DFS flatten (roots_ honoring the expanded
    // set) so a list row index maps back to its node — needed by the
    // wrapped item-click (double-click + chevron replication).
    struct FlatRef {
        const XER::UI::Widgets::TreeNode* node = nullptr;
        int depth = 0;
    };
    std::vector<FlatRef> FlatRows() const;
    int ListIndexAt(XER::UI::Layout::Point point) const;
    void ToggleFavorite(const std::string& type);
    void ApplyType(const std::string& type);

    std::vector<FxEntry> entries_;
    std::vector<XER::UI::Widgets::TreeNode> roots_;   // model copy
    std::set<std::string> favorites_;
    std::set<std::string> saved_expanded_;            // pre-search state
    std::string search_;

    std::shared_ptr<XER::UI::Widgets::TextInput> search_input_;
    std::shared_ptr<XER::UI::Widgets::TreeView> tree_;
    std::shared_ptr<XER::UI::Widgets::Button> apply_btn_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;

    // Double-click synthesis for leaf rows.
    std::string last_click_id_;
    std::chrono::steady_clock::time_point last_click_time_{};
    XER::UI::Layout::Point last_click_point_{};

    bool children_attached_ = false;
    bool syncing_selection_ = false;

    std::function<void(const std::vector<std::string>&,
                       const std::string&)>
        on_apply_fx_;
    std::function<std::vector<std::string>()> clip_provider_;
    std::function<void(const std::string&, bool)> on_status_;
};

}  // namespace XER::VideoEditor::Ui2
