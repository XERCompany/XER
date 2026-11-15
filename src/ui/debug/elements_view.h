#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../widgets/widget.h"

namespace XER::UI::Widgets {
class Widget;
}

namespace XER::UI::Debug {

/**
 * ElementsView — DevTools-style widget tree panel.
 *
 * Renders a flat, depth-indented list of every widget in the inspected
 * tree with expander arrows, type names, bounds and state markers.
 * Expand/collapse state and selection persist across Refresh() calls.
 *
 * Designed to live inside a ScrollArea: scrolling, clipping and the
 * scrollbar are provided by the container — this widget only reports
 * its content height and maps clicks to rows.
 */
class ElementsView final : public Widgets::Widget {
public:
    ElementsView();

    /// Attaches the inspected tree root and rebuilds the row model.
    void SetRoot(std::shared_ptr<Widgets::Widget> root);
    std::shared_ptr<Widgets::Widget> GetRoot() const { return root_; }

    /// Rebuilds rows from the current tree, preserving expansion and
    /// selection where the widgets still exist.
    void Refresh();

    void SetOnSelect(
        std::function<void(std::shared_ptr<Widgets::Widget>)> cb) {
        on_select_ = std::move(cb);
    }

    std::shared_ptr<Widgets::Widget> GetSelected() const { return selected_; }
    void Select(std::shared_ptr<Widgets::Widget> widget);

    /// Row height in DIPs — exposed so the owner can align details.
    float GetRowHeight() const { return row_height_; }

    void Render(Rendering::Renderer& renderer,
                const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Widgets::Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;
    float GetContentHeight() const override {
        return static_cast<float>(rows_.size()) * row_height_;
    }

private:
    struct Row {
        std::shared_ptr<Widgets::Widget> widget;
        int depth = 0;
        bool has_children = false;
    };

    void Rebuild();
    void Collect(std::shared_ptr<Widgets::Widget> w, int depth);
    bool IsExpanded(const Widgets::Widget& w, int depth) const;
    void ToggleExpanded(const Widgets::Widget& w);
    int RowAtY(float local_y) const;
    bool PointOnExpander(const Row& row, const Layout::Point& p) const;
    void ActivateRow(int index);

    std::shared_ptr<Widgets::Widget> root_;
    std::vector<Row> rows_;
    // Explicit user toggles; absent entries fall back to the default
    // (expanded for shallow nodes, collapsed deeper in the tree).
    std::unordered_map<const Widgets::Widget*, bool> expanded_;
    std::shared_ptr<Widgets::Widget> selected_;
    int hovered_ = -1;
    float row_height_ = 24.0f;
    bool needs_rebuild_ = true;
    std::function<void(std::shared_ptr<Widgets::Widget>)> on_select_;
};

} // namespace XER::UI::Debug
