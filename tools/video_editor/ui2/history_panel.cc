// HistoryPanel — see history_panel.h for the ordering contract.
// Rows live in ONE VirtualList: [0, UndoCount()) undo entries
// rendered newest-on-top, a "current state" divider row, then the
// redo entries (next-to-redo first). Clicking a row fires
// on_undo_steps/on_redo_steps with the row's divider distance —
// the shell repeats Undo()/Redo() that many times (no direct
// jump-to-depth op exists on TimelineHistory).
#include "history_panel.h"

#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/virtual_list.h"

#ifdef DrawText
#undef DrawText
#endif

namespace XER::VideoEditor::Ui2 {
namespace {

namespace W = XER::UI::Widgets;
namespace Layout = XER::UI::Layout;

constexpr float kToolbarH = 30.0f;
constexpr float kToolbarGap = 6.0f;
constexpr float kStatusH = 18.0f;
constexpr float kRowH = 26.0f;

}  // namespace

std::shared_ptr<HistoryPanel> HistoryPanel::Create() {
    return std::shared_ptr<HistoryPanel>(new HistoryPanel());
}

HistoryPanel::HistoryPanel() {
    SetFocusable(true);
    SetClipsChildren(true);
}

void HistoryPanel::SetHooks(Hooks hooks) { hooks_ = std::move(hooks); }

void HistoryPanel::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;

    counts_ = std::make_shared<W::Label>("", true);

    clear_btn_ = std::make_shared<W::Button>(
        "Clear", W::ButtonStyle::Ghost);
    clear_btn_->SetSize(W::ButtonSize::Small);
    clear_btn_->SetTooltip("Clear the undo/redo history");
    clear_btn_->SetOnClick([this] { ClearHistory(); });

    list_ = W::VirtualList::Create();
    list_->SetItemHeight(kRowH);
    list_->SetSelectionMode(W::VirtualList::SelectionMode::None);
    list_->SetItemRenderer(
        [this](XER::UI::Rendering::Renderer& renderer,
               Layout::Rect row, int index, bool /*selected*/) {
            const auto& tokens = CachedLayoutTokens();
            const float pad = Theme::kSpaceXs;
            if (KindAt(index) == RowKind::Divider) {
                // Current-state divider: accent edge + rule + label.
                renderer.FillRect(
                    {row.x + pad, row.y + 2.0f, 3.0f,
                     row.height - 4.0f},
                    tokens.accent, 1.5f);
                const float cy = row.y + row.height * 0.5f;
                const float lx = row.x + pad + 10.0f;
                const float lw = 84.0f;
                renderer.DrawText(
                    "current state",
                    {lx, row.y + 4.0f, lw, row.height - 8.0f},
                    {tokens.accent, Theme::kFontCaption, 600});
                renderer.DrawLine({lx + lw + 6.0f, cy},
                                  {row.Right() - pad, cy},
                                  tokens.border, 1.0f);
                return;
            }
            const bool is_redo = KindAt(index) == RowKind::Redo;
            const int steps = StepsAt(index);
            // Steps chip "-N" / "+N".
            char tag[8];
            std::snprintf(tag, sizeof(tag), "%c%d",
                          is_redo ? '+' : '-', steps);
            const float chip_w = 34.0f;
            renderer.DrawText(
                tag,
                {row.Right() - chip_w - pad, row.y + 4.0f, chip_w,
                 row.height - 8.0f},
                {tokens.muted, Theme::kFontCaption, 500, true});
            // Label — undo rows foreground, redo rows muted.
            const float label_w =
                row.width - chip_w - pad * 3.0f;
            const XER::UI::Rendering::TextStyle label_style{
                is_redo ? tokens.muted : tokens.foreground,
                Theme::kFontBody, 400};
            renderer.DrawText(
                W::EllipsizeText(renderer, LabelAt(index),
                                 label_style, label_w),
                {row.x + pad + 4.0f, row.y + 4.0f, label_w,
                 row.height - 8.0f},
                label_style);
        });
    list_->SetOnItemClick(
        [this](int index, Layout::Point) -> bool {
            JumpToRow(index);
            return true;   // no selection semantics — consume
        });
    list_->SetOnItemActivated(
        [this](int index) { JumpToRow(index); });
    list_->SetTooltip("Click an entry to jump through history");

    status_ = std::make_shared<W::Label>("no history", true);

    AddChild(counts_);
    AddChild(clear_btn_);
    AddChild(list_);
    AddChild(status_);
    SyncCounts();
}

void HistoryPanel::SetEntries(
    const std::vector<std::string>& undo_labels,
    const std::vector<std::string>& redo_labels) {
    undo_ = undo_labels;
    redo_ = redo_labels;
    if (list_ != nullptr) list_->SetItemCount(RowCount());
    SyncCounts();
    Invalidate();
}

HistoryPanel::RowKind HistoryPanel::KindAt(int row) const {
    const int u = static_cast<int>(undo_.size());
    if (row < 0 || row >= RowCount()) return RowKind::Divider;
    if (row < u) return RowKind::Undo;
    if (row == u) return RowKind::Divider;
    return RowKind::Redo;
}

std::string HistoryPanel::LabelAt(int row) const {
    const int u = static_cast<int>(undo_.size());
    if (row < 0 || row >= RowCount() || row == u) return {};
    if (row < u) {
        // undo_ is oldest-first; row 0 shows the NEWEST label.
        return undo_[static_cast<std::size_t>(u - 1 - row)];
    }
    return redo_[static_cast<std::size_t>(row - u - 1)];
}

int HistoryPanel::StepsAt(int row) const {
    const int u = static_cast<int>(undo_.size());
    if (row < 0 || row >= RowCount() || row == u) return 0;
    return row < u ? row + 1 : row - u;
}

bool HistoryPanel::JumpToRow(int row) {
    const int u = static_cast<int>(undo_.size());
    if (row < 0 || row >= RowCount() || row == u) return false;
    if (row < u) {
        if (hooks_.on_undo_steps) hooks_.on_undo_steps(row + 1);
    } else {
        if (hooks_.on_redo_steps) hooks_.on_redo_steps(row - u);
    }
    return true;
}

void HistoryPanel::ClearHistory() {
    if (hooks_.on_clear) hooks_.on_clear();
}

void HistoryPanel::SyncCounts() {
    const char* mid = " \xc2\xb7 ";   // "·"
    char counts[48];
    std::snprintf(counts, sizeof(counts), "%d undo%s%d redo",
                  static_cast<int>(undo_.size()), mid,
                  static_cast<int>(redo_.size()));
    if (counts_ != nullptr) counts_->SetText(counts);
    if (status_ != nullptr) {
        status_->SetText(undo_.empty() && redo_.empty()
                             ? "No history — edits appear here"
                             : "Click an entry to jump");
    }
    if (clear_btn_ != nullptr) {
        clear_btn_->SetEnabled(!undo_.empty() || !redo_.empty());
    }
}

void HistoryPanel::Render(XER::UI::Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens) {
    renderer.FillRect(GetBounds(), tokens.panel, 0.0f);
}

void HistoryPanel::Layout(const Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const float x = bounds.x + Theme::kSpaceSm;
    float y = bounds.y + Theme::kSpaceSm;
    const float w = bounds.width - Theme::kSpaceSm * 2.0f;
    const auto& tokens = CachedLayoutTokens();

    // Row 1: "N undo · M redo" caption (flex) + Clear.
    const float clear_w = 56.0f;
    counts_->Layout(
        {x, y + 5.0f, w - clear_w - kToolbarGap, kToolbarH - 8.0f},
        tokens);
    clear_btn_->Layout({x + w - clear_w, y, clear_w, kToolbarH},
                       tokens);
    y += kToolbarH + kToolbarGap;

    const float content_h =
        bounds.y + bounds.height - Theme::kSpaceSm - kStatusH - y;
    const Layout::Rect content{x, y, w, std::max(0.0f, content_h)};
    list_->Layout(content, tokens);
    status_->Layout(
        {x, y + std::max(0.0f, content_h) + 2.0f, w, kStatusH},
        tokens);
}

void HistoryPanel::Tick(double /*dt_seconds*/) {}

}  // namespace XER::VideoEditor::Ui2
