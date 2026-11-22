// HistoryPanel — the M3 wave-2 history dock (spec §6 "Hist" tab):
// After-Effects-style undo/redo stack browser rebuilt on VirtualList.
//
//   HISTORY                    3 undo · 1 redo          [Clear]
//   ┌────────────────────────────────────────────────┐
//   │ Trim clip "intro"                        [-1]  │ click: undo 1
//   │ Move clip "b-roll"                     [-2]  │ click: undo 2
//   │ Add clip                             [-3]  │ click: undo 3
//   │ ────────── current state ──────────           │ accent edge
//   │ Add marker                           [+1]  │ dimmed: redo 1
//   └────────────────────────────────────────────────┘
//
// The shell fills both stacks via SetEntries after every history
// mutation — TimelineHistory::UndoLabels/RedoLabels already expose
// the full enumeration (oldest→newest / next-to-redo first), so no
// core API change is needed.
//
// Ordering contract (same as the legacy panel):
//   * undo labels render NEWEST ON TOP; clicking undo row i emits
//     on_undo_steps(i + 1);
//   * redo rows sit below the divider; clicking redo row j emits
//     on_redo_steps(j + 1);
//   * the divider row is the highlighted "current state" — clicking
//     it is a no-op.
//
// Jump granularity note: TimelineHistory exposes Undo()/Redo() with
// no direct "jump to depth" op, so the shell implements a click as N
// repeated Undo()/Redo() calls — correct-by-construction since every
// row's distance from the divider is exactly its step count.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
class VirtualList;
}

namespace XER::VideoEditor::Ui2 {

class HistoryPanel final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<HistoryPanel> Create();

    struct Hooks {
        // N steps to undo/redo in one gesture (N >= 1 = the clicked
        // row's distance from the current-state divider). The shell
        // loops TimelineHistory::Undo()/Redo() that many times.
        std::function<void(int steps)> on_undo_steps;
        std::function<void(int steps)> on_redo_steps;
        // Toolbar Clear — empties both stacks (history_->Clear()).
        std::function<void()> on_clear;
        std::function<void(const std::string&, bool)> on_status;
    };
    void SetHooks(Hooks hooks);
    const Hooks& GetHooks() const { return hooks_; }

    // Replaces both stacks — call again after every history
    // mutation (NotifyTimelineChanged on the shell side).
    void SetEntries(const std::vector<std::string>& undo_labels,
                    const std::vector<std::string>& redo_labels);
    int UndoCount() const { return static_cast<int>(undo_.size()); }
    int RedoCount() const { return static_cast<int>(redo_.size()); }

    // Virtual row space: [0, UndoCount()) undo rows newest-first,
    // one divider row at UndoCount(), then redo rows.
    int RowCount() const {
        return static_cast<int>(undo_.size() + redo_.size()) + 1;
    }
    enum class RowKind { Undo, Divider, Redo };
    RowKind KindAt(int row) const;
    // Display label + step distance for a row ("" / 0 on divider).
    std::string LabelAt(int row) const;
    int StepsAt(int row) const;

    // The click path — the same funnel VirtualList activation and
    // the smoke drive. Returns false on the divider/out-of-range.
    bool JumpToRow(int row);
    void ClearHistory();   // toolbar verb

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {200.0f, 140.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {280.0f, 300.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit HistoryPanel();

    void EnsureChildren();
    void SyncCounts();

    std::vector<std::string> undo_;   // oldest first (model order)
    std::vector<std::string> redo_;   // next-to-redo first

    std::shared_ptr<XER::UI::Widgets::Label> counts_;
    std::shared_ptr<XER::UI::Widgets::Button> clear_btn_;
    std::shared_ptr<XER::UI::Widgets::VirtualList> list_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;

    bool children_attached_ = false;
    Hooks hooks_;
};

}  // namespace XER::VideoEditor::Ui2
