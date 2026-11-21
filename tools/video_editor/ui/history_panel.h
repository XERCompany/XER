// History panel: After-Effects-style undo/redo stack browser.
//
//   HISTORY                          3 undo · 1 redo
//   ┌──────────────────────────────────────────────┐
//   │ Trim clip "intro"                    [-1]    │  click = undo 1 step
//   │ Move clip "b-roll"                   [-2]    │  click = undo 2 steps
//   │ Add clip                           [-3]    │  click = undo 3 steps
//   │ ──────────── current state ────────────────  │
//   │ add marker                         [-1]    │  dimmed, click = redo 1
//   └──────────────────────────────────────────────┘
//
// The host fills the stacks via SetEntries() after every history
// mutation (TimelineHistory exposes only the top labels, so the app
// mirrors command labels as it executes/undoes/redoes them).
//
// Ordering contract:
//   * undo_labels — oldest first, LAST element = most recent action
//     (natural std::deque/stack push_back order). Rendered newest on
//     top; clicking row i (0 = top) emits on_undo_steps(i + 1).
//   * redo_labels — next-to-redo FIRST (element 0 is what Redo() would
//     re-apply). Clicking row j emits on_redo_steps(j + 1).
//
// Rows are Widgets::Item children (undo entries via the title slot,
// redo via the muted description slot) with a trailing "-N" steps
// Badge; the current-state divider is two Separators flanking a muted
// Label. All row children are DISABLED — Click events do not bubble
// from children to the panel, but disabled widgets are skipped by
// hit-testing, so row clicks still land on the panel for the
// jump-through-history gesture. The list scrolls internally with the
// mouse wheel when it exceeds the viewport (SetClipsChildren keeps
// rows inside the panel chrome).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Badge;
class Item;
class Label;
class Separator;
}

namespace XER::VideoEditor::Ui {

class HistoryPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // N steps to undo/redo in one gesture (N = distance of the
        // clicked row from the current-state divider, >= 1). The host
        // loops TimelineHistory::Undo()/Redo() that many times.
        std::function<void(int steps)> on_undo_steps;
        std::function<void(int steps)> on_redo_steps;
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit HistoryPanelWidget(Hooks hooks = {});

    // Replaces both stacks (see the header comment for the ordering
    // contract). Either vector may be empty. Call again after every
    // history mutation to refresh.
    void SetEntries(const std::vector<std::string>& undo_labels,
                    const std::vector<std::string>& redo_labels);
    int UndoCount() const { return static_cast<int>(undo_.size()); }
    int RedoCount() const { return static_cast<int>(redo_.size()); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 140.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {280.0f, 260.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct Row {
        // Both disabled: purely presentational — hit-testing skips
        // them so row clicks reach the panel (no click bubbling).
        std::shared_ptr<XER::UI::Widgets::Item> item;
        std::shared_ptr<XER::UI::Widgets::Badge> steps;
    };

    void EnsureChildren();
    // Rebuilds the per-entry Item+Badge children. No retired-row
    // graveyard is needed: every row child is disabled, so none can
    // ever be the dispatching event target when the host calls back
    // into SetEntries mid-click.
    void RebuildRows();
    // Virtual row space: [0, undo_.size()) undo rows, then one divider
    // row, then redo rows. Returns -1 outside the list viewport.
    int RowAt(const XER::UI::Layout::Point& position) const;
    XER::UI::Layout::Rect RowRect(int row) const;
    int RowCount() const;   // undo + 1 divider + redo
    float ScrollRange() const;
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    std::vector<std::string> undo_;   // oldest first
    std::vector<std::string> redo_;   // next-to-redo first
    int hovered_ = -1;
    float scroll_y_ = 0.0f;
    float rows_top_ = 0.0f;           // layout: y where the list begins
    float rows_bottom_ = 0.0f;        // layout: list viewport bottom
    float row_height_ = 26.0f;
    bool children_attached_ = false;

    std::vector<Row> rows_;   // display order: undos, then redos
    std::shared_ptr<XER::UI::Widgets::Label> counts_;
    // Current-state divider row: rule — caption — rule.
    std::shared_ptr<XER::UI::Widgets::Separator> divider_left_;
    std::shared_ptr<XER::UI::Widgets::Label> divider_label_;
    std::shared_ptr<XER::UI::Widgets::Separator> divider_right_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
