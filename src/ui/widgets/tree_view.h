#ifndef XER_UI_WIDGETS_TREE_VIEW_H
#define XER_UI_WIDGETS_TREE_VIEW_H

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "widgets/virtual_list.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

// One node of a TreeView hierarchy (drift-ui tree-view contract — see
// docs/wiki/research/2026-11-22-registry-data-widgets.md).
//
// `is_branch` marks a node that MAY have children: it gets an
// expand/collapse chevron even before any children exist. A branch with
// no children is LAZY — expanding it fires the SetLoadChildren callback
// and shows a "Loading…" placeholder row until the host calls
// SetNodeChildren (or SetNodeLoadFailed, which swaps in a retry row).
struct TreeNode {
    std::string id;          // stable identity — selection/expansion keys
    std::string label;
    std::string icon;        // optional image path (Renderer::DrawImage)
    std::vector<TreeNode> children;
    bool is_branch = false;  // may have children (lazy when empty)
    bool disabled = false;   // muted, not selectable/checkable via input
};

// TreeView — user-facing expandable tree (gap G3). Dense 24-28 DIP rows,
// indent per depth, expand/collapse chevrons, hover/selected/cursor
// chrome, keyboard nav + label-prefix typeahead.
//
// ARCHITECTURE: composition over re-implementation. The tree flattens its
// visible nodes into a row vector (DFS respecting the expanded set, plus
// Loading/Retry placeholder rows for lazy branches) and drives an
// internal VirtualList child with that count + an item renderer that
// draws chevron/checkbox/icon/label for each row. The list owns all
// generic machinery — scroll offset, floating scrollbar, viewport
// culling, hover, up/down/page/home/end cursor — while the tree owns the
// id-keyed data semantics: flat index ↔ node id mapping in both
// directions, expansion, tri-state checks, lazy loading, Left/Right
// tree-specific keys via the list's key-fallback hook. The alternative
// (own flatten + own scroll/keys, like Table) would duplicate ~200 lines
// of well-tested scroll machinery for zero added capability.
//
// Selection/expanded state is CONTROLLED (id-keyed sets with Set/Get +
// change callbacks). Callbacks fire only on user-driven changes — Set*
// methods stay silent, matching kit convention.
//
// Typeahead note: our Event model reports letter KeyCodes only while
// Ctrl is held (widget.h), so typeahead is implemented through TextInput
// events — typing printable characters jumps to the next visible row
// whose label starts with the accumulated prefix (~900 ms window).
class TreeView final : public Widget {
public:
    static std::shared_ptr<TreeView> Create();

    // ---- model ---------------------------------------------------------
    void SetItems(std::vector<TreeNode> roots);
    // Lazy-load result: replaces the children of node `id`, marks it
    // loaded, and — in multiple mode — a checked parent cascades its
    // check onto the new descendants (drift semantics). Safe to call
    // from a worker-completion hop on the UI thread.
    void SetNodeChildren(const std::string& id,
                         std::vector<TreeNode> children);
    // Marks a lazy load as failed: the branch's child row becomes a
    // "Retry" row the user can click (or ArrowRight on) to re-fire the
    // load callback.
    void SetNodeLoadFailed(const std::string& id, std::string error);

    // ---- expansion (controlled, id-keyed) -------------------------------
    void SetExpanded(std::set<std::string> ids);
    const std::set<std::string>& GetExpanded() const { return expanded_; }
    bool IsExpanded(const std::string& id) const {
        return expanded_.count(id) != 0;
    }
    void SetNodeExpanded(const std::string& id, bool expanded);
    void ExpandAll();
    void CollapseAll();
    void SetOnExpandChanged(
        std::function<void(const std::string& id, bool expanded)> cb);

    // ---- selection (controlled, id-keyed) -------------------------------
    // SetMultiple(true) switches to checkbox mode: the selection set IS
    // the checked set, branches report a tri-state (unchecked / partial /
    // checked) derived from their loaded descendants, and checking a
    // branch cascades to all loaded descendants.
    void SetMultiple(bool multiple);
    bool IsMultiple() const { return multiple_; }
    void SetSelectedId(std::string id);  // "" clears (single mode)
    std::string GetSelectedId() const;
    void SetSelectedIds(std::set<std::string> ids);
    const std::set<std::string>& GetSelectedIds() const {
        return selected_ids_;
    }
    void SetOnSelectionChanged(std::function<void()> cb);

    // ---- lazy loading ----------------------------------------------------
    // Fired once per expansion of a lazy branch (is_branch && no
    // children && never loaded). The host performs the load (sync or
    // async) and reports back through SetNodeChildren/SetNodeLoadFailed.
    void SetLoadChildren(
        std::function<void(TreeView& tree, const std::string& id)> cb);

    // ---- appearance ------------------------------------------------------
    void SetRowHeight(float px);    // forwarded to the list (def 26 DIP)
    void SetIndentSize(float px);   // indent per depth level (def 16 DIP)
    float GetIndentSize() const { return indent_size_; }

    // ---- scrolling --------------------------------------------------------
    void EnsureVisible(const std::string& id);
    void ScrollToNode(const std::string& id);
    // Escape hatch for hosts that need list-level control (overscan,
    // scroll position, painted range). The tree keeps its own state in
    // sync via the list's callbacks — don't replace them.
    std::shared_ptr<VirtualList> GetList() const { return list_; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;
    float GetContentHeight() const override {
        return static_cast<float>(flat_.size()) * row_height_;
    }

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    explicit TreeView();

    enum class RowKind { Node, Loading, Retry };
    // Absent from the map = Unloaded; Loaded marks branches that returned
    // children (even empty ones) so they never re-fire the callback.
    enum class LoadState { Loading, Failed, Loaded };
    enum class CheckState : int { Unchecked = 0, Partial = 1, Checked = 2 };

    // One row of the flattened (visible) tree. `node` is the node a Node
    // row renders; `parent` is the lazy branch a Loading/Retry row
    // belongs to. Pointers stay valid because every mutation re-flattens
    // immediately — flat_ never observes a mutated tree.
    struct FlatRow {
        RowKind kind = RowKind::Node;
        TreeNode* node = nullptr;
        TreeNode* parent = nullptr;
        int depth = 0;
    };

    void Reflatten();
    void FlattenInto(std::vector<TreeNode>& nodes, int depth);
    TreeNode* FindNode(const std::string& id);
    const TreeNode* FindNode(const std::string& id) const;
    int FlatIndexOf(const std::string& id) const;
    void CollectIds(TreeNode& node, std::vector<std::string>* out) const;
    CheckState NodeCheckState(const TreeNode& node) const;

    // VirtualList callback implementations.
    void RenderRow(Rendering::Renderer& renderer, Layout::Rect row,
                   int index);
    bool RowClick(int index, Layout::Point position);
    void ActivateRow(int index);
    bool FallbackKey(const Event& event);
    void PullSelectionFromList();
    void PushSelectionToList();

    // Tree semantics on top of the flat row model.
    void ToggleExpanded(TreeNode& node);
    void RetryLoad(TreeNode& parent);
    void ToggleCheck(TreeNode& node);
    void SetCheckedCascade(TreeNode& node, bool checked);
    void MoveCursorToParent(int flat_index);
    void Typeahead(char ch);
    // DFS: marks every expanded lazy branch still Unloaded as Loading and
    // fires the load callback for it (exactly-once — see .cc). Called
    // from every expansion entry point.
    void FireLazyLoads();
    void ComputeCheckStates(const std::vector<TreeNode>& nodes);
    // Inner rect a row's content starts at: depth indent + leading pad.
    Layout::Rect RowContentRect(const FlatRow& row,
                                const Layout::Rect& rect) const;

    std::vector<TreeNode> roots_;
    std::vector<FlatRow> flat_;
    std::shared_ptr<VirtualList> list_;

    std::set<std::string> expanded_;
    std::set<std::string> selected_ids_;
    std::map<std::string, LoadState> load_state_;
    std::map<std::string, std::string> load_errors_;
    // Tri-state per branch id, rebuilt by Reflatten (post-order DFS).
    std::map<std::string, CheckState> check_state_;

    bool multiple_ = false;
    float row_height_ = 26.0f;
    float indent_size_ = 16.0f;

    // Typeahead accumulation (~900 ms window between keystrokes).
    std::string typeahead_;
    std::chrono::steady_clock::time_point last_typeahead_{};

    std::function<void(const std::string&, bool)> on_expand_changed_;
    std::function<void()> on_selection_changed_;
    std::function<void(TreeView&, const std::string&)> load_children_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_TREE_VIEW_H
