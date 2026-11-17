#include "history_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

}  // namespace

HistoryPanelWidget::HistoryPanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // the stack scrolls inside the panel
}

void HistoryPanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;
    title_ = std::make_shared<Widgets::Label>("HISTORY");
    status_ = std::make_shared<Widgets::Label>("no history", true);
    AddChild(title_);
    AddChild(status_);
}

void HistoryPanelWidget::SetEntries(
    const std::vector<std::string>& undo_labels,
    const std::vector<std::string>& redo_labels) {
    undo_ = undo_labels;
    redo_ = redo_labels;
    hovered_ = -1;
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
    if (children_attached_) {
        status_->SetText(undo_.empty() && redo_.empty()
                             ? "no history"
                             : "click an entry to jump");
    }
    Invalidate();
}

int HistoryPanelWidget::RowCount() const {
    return static_cast<int>(undo_.size() + redo_.size()) + 1;
}

float HistoryPanelWidget::ScrollRange() const {
    const float view_h = rows_bottom_ - rows_top_;
    return std::max(
        0.0f, static_cast<float>(RowCount()) * row_height_ -
                  std::max(view_h, 0.0f));
}

Rect HistoryPanelWidget::RowRect(int row) const {
    const auto bounds = GetBounds();
    return {bounds.x + 8.0f,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - 16.0f, row_height_ - 3.0f};
}

int HistoryPanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    const auto bounds = GetBounds();
    if (position.y < rows_top_ || position.y >= rows_bottom_ ||
        position.x < bounds.x || position.x >= bounds.x + bounds.width) {
        return -1;
    }
    const int row = static_cast<int>(
        std::floor((position.y - rows_top_ + scroll_y_) / row_height_));
    return row >= 0 && row < RowCount() ? row : -1;
}

void HistoryPanelWidget::SetStatus(const std::string& text,
                                   bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void HistoryPanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);

    // Header counts: "N undo · M redo" on the right of the title row.
    char counts[48];
    std::snprintf(counts, sizeof(counts), "%d undo \xc2\xb7 %d redo",
                  static_cast<int>(undo_.size()),
                  static_cast<int>(redo_.size()));
    renderer.DrawText(counts,
                      {bounds.x + bounds.width - 150.0f,
                       bounds.y + 8.0f, 142.0f, 18.0f},
                      XER::UI::Rendering::TextStyle{
                          tokens.muted, tokens.font_small, 500});

    if (rows_top_ <= 0.0f) return;   // not yet laid out
    if (undo_.empty() && redo_.empty()) {
        renderer.DrawText("edits made through the timeline appear here",
                          {bounds.x + 12.0f, rows_top_ + 8.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    const int undo_count = static_cast<int>(undo_.size());
    const int divider_row = undo_count;
    for (int row = 0; row < RowCount(); ++row) {
        const Rect r = RowRect(row);
        if (r.y + r.height < rows_top_ || r.y > rows_bottom_) continue;

        if (row == divider_row) {
            // Current-state divider: rule + centered caption. Not
            // clickable — it marks where the project stands.
            const float cy = r.y + r.height * 0.5f;
            renderer.DrawLine({r.x + 4.0f, cy}, {r.Right() - 4.0f, cy},
                              WithAlpha(tokens.accent, 0.6f), 1.0f);
            renderer.DrawText("current state",
                              {r.x, r.y + 2.0f, r.width,
                               r.height - 4.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.accent, tokens.font_small, 600});
            continue;
        }

        const bool is_undo = row < divider_row;
        if (row == hovered_) {
            renderer.FillRect(r, tokens.hover, tokens.radius_small);
        }
        // Entry label: undo stack is stored oldest-first so the newest
        // (undo_.back()) draws on top; redo rows sit below the divider.
        const std::string& label =
            is_undo
                ? undo_[static_cast<std::size_t>(undo_count - 1 - row)]
                : redo_[static_cast<std::size_t>(row - divider_row - 1)];
        const Color text_color =
            is_undo ? tokens.foreground : tokens.muted;
        renderer.DrawText(label,
                          {r.x + 8.0f, r.y + 3.0f, r.width - 46.0f,
                           r.height - 6.0f},
                          XER::UI::Rendering::TextStyle{
                              text_color, tokens.font_body, 400});
        // Distance-from-now tag on the right edge.
        const int steps =
            is_undo ? row + 1 : row - divider_row;
        char tag[8];
        std::snprintf(tag, sizeof(tag), "-%d", steps);
        renderer.DrawText(tag,
                          {r.Right() - 36.0f, r.y + 4.0f, 32.0f,
                           r.height - 8.0f},
                          XER::UI::Rendering::TextStyle{
                              WithAlpha(tokens.muted, 0.7f),
                              tokens.font_small, 500});
    }
}

void HistoryPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    rows_top_ = bounds.y + 34.0f + tokens.spacing_small;
    rows_bottom_ = bounds.y + bounds.height - 30.0f;
    status_->Layout({bounds.x + pad, bounds.y + bounds.height - 26.0f,
                     bounds.width - pad * 2.0f, 20.0f},
                    tokens);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
}

bool HistoryPanelWidget::HandleEvent(
    const XER::UI::Widgets::Event& event) {
    switch (event.type) {
        case XER::UI::Widgets::Event::Type::MouseWheel: {
            const float range = ScrollRange();
            if (range <= 0.0f) return false;
            scroll_y_ = std::clamp(
                scroll_y_ - static_cast<float>(event.wheel_delta) * 0.5f,
                0.0f, range);
            Invalidate();
            return true;
        }
        case XER::UI::Widgets::Event::Type::MouseMove: {
            const int row = RowAt(event.position);
            if (row != hovered_) {
                hovered_ = row;
                Invalidate();
            }
            return false;
        }
        case XER::UI::Widgets::Event::Type::Click: {
            const int row = RowAt(event.position);
            if (row < 0) return false;
            const int undo_count = static_cast<int>(undo_.size());
            if (row < undo_count) {
                const int steps = row + 1;
                if (hooks_.on_undo_steps) hooks_.on_undo_steps(steps);
                SetStatus(steps == 1 ? "undid 1 step"
                                     : "undid " + std::to_string(steps) +
                                           " steps");
                return true;
            }
            if (row > undo_count) {
                const int steps = row - undo_count;
                if (hooks_.on_redo_steps) hooks_.on_redo_steps(steps);
                SetStatus(steps == 1 ? "redid 1 step"
                                     : "redid " + std::to_string(steps) +
                                           " steps");
                return true;
            }
            return true;   // divider row: consume, no-op
        }
        default:
            return false;
    }
}

void HistoryPanelWidget::Tick(double dt_seconds) { (void)dt_seconds; }

}  // namespace XER::VideoEditor::Ui
