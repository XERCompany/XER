#include "history_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "icon_helpers.h"
#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/badge.h"
#include "widgets/item.h"
#include "widgets/label.h"
#include "widgets/separator.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;

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
    // "N undo · M redo" — muted caption right of the header band.
    counts_ = std::make_shared<Widgets::Label>("", true);
    // Current-state divider: Separator — muted Label — Separator.
    // All three disabled so they stay transparent to hit-testing.
    divider_left_ = std::make_shared<Widgets::Separator>(
        Widgets::Orientation::Horizontal);
    divider_label_ =
        std::make_shared<Widgets::Label>("current state", true);
    divider_right_ = std::make_shared<Widgets::Separator>(
        Widgets::Orientation::Horizontal);
    divider_left_->SetEnabled(false);
    divider_label_->SetEnabled(false);
    divider_right_->SetEnabled(false);
    status_ = std::make_shared<Widgets::Label>("no history", true);
    AddChild(counts_);
    AddChild(divider_left_);
    AddChild(divider_label_);
    AddChild(divider_right_);
    AddChild(status_);
    RebuildRows();
    // EnsureChildren can run after the first SetEntries — sync the
    // caption here too so it never lags one update behind.
    char counts[48];
    std::snprintf(counts, sizeof(counts), "%d undo \xc2\xb7 %d redo",
                  static_cast<int>(undo_.size()),
                  static_cast<int>(redo_.size()));
    counts_->SetText(counts);
}

void HistoryPanelWidget::SetEntries(
    const std::vector<std::string>& undo_labels,
    const std::vector<std::string>& redo_labels) {
    undo_ = undo_labels;
    redo_ = redo_labels;
    hovered_ = -1;
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
    if (children_attached_) {
        char counts[48];
        std::snprintf(counts, sizeof(counts), "%d undo \xc2\xb7 %d redo",
                      static_cast<int>(undo_.size()),
                      static_cast<int>(redo_.size()));
        counts_->SetText(counts);
        status_->SetText(undo_.empty() && redo_.empty()
                             ? "no history"
                             : "click an entry to jump");
        RebuildRows();
    }
    Invalidate();
}

void HistoryPanelWidget::RebuildRows() {
    namespace Widgets = XER::UI::Widgets;
    for (Row& row : rows_) {
        RemoveChild(row.item);
        RemoveChild(row.steps);
    }
    rows_.clear();
    rows_.reserve(undo_.size() + redo_.size());

    const int undo_count = static_cast<int>(undo_.size());
    const auto make_row = [this](const std::string& label, int steps,
                                 bool redo) {
        Row row;
        row.item = std::make_shared<Widgets::Item>();
        // Undo rows use the title slot (foreground, body); redo rows
        // use the description slot (muted, small) so future entries
        // read dimmed — the same hierarchy the drawn list had.
        if (redo) {
            row.item->SetDescription(label);
        } else {
            row.item->SetTitle(label);
        }
        row.item->SetEnabled(false);
        char tag[8];
        std::snprintf(tag, sizeof(tag), "-%d", steps);
        row.steps = std::make_shared<Widgets::Badge>(
            tag, Widgets::BadgeVariant::Outline);
        // Disabled also gives the Badge its quiet disabled paint —
        // right for a metadata tag — and keeps it out of hit-testing.
        row.steps->SetEnabled(false);
        AddChild(row.item);
        AddChild(row.steps);
        rows_.push_back(std::move(row));
    };
    // Undo stack renders newest on top (undo_.back() first); the row's
    // distance from the divider is its step count.
    for (int i = 0; i < undo_count; ++i) {
        make_row(undo_[static_cast<std::size_t>(undo_count - 1 - i)],
                 i + 1, false);
    }
    for (std::size_t i = 0; i < redo_.size(); ++i) {
        make_row(redo_[i], static_cast<int>(i) + 1, true);
    }
    RequestLayout();
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
    return {bounds.x + VeTheme::kSpaceSm,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - VeTheme::kSpaceSm * 2.0f,
            row_height_ - 3.0f};
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
    // Collapsible-style panel header — the shared VeIcons chrome.
    const float pad = tokens.spacing_medium;
    VeIcons::DrawSectionHeader(
        renderer, tokens,
        {bounds.x + pad, bounds.y + pad * 0.6f,
         bounds.width - pad * 2.0f, VeTheme::kRowHeight},
        "History", false);

    if (rows_top_ <= 0.0f) return;   // not yet laid out
    if (undo_.empty() && redo_.empty()) {
        renderer.DrawText("edits made through the timeline appear here",
                          {bounds.x + 12.0f, rows_top_ + 8.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    // Rows paint themselves (Item + Badge children); the panel only
    // draws the hover wash behind the entry under the cursor. The
    // divider row is Separator/Label children — nothing to draw.
    const int undo_count = static_cast<int>(undo_.size());
    for (int row = 0; row < RowCount(); ++row) {
        if (row == undo_count) continue;   // divider row
        const Rect r = RowRect(row);
        if (r.y + r.height < rows_top_ || r.y > rows_bottom_) continue;
        if (row == hovered_) {
            renderer.FillRect(r, tokens.hover, tokens.radius_small);
        }
    }
}

void HistoryPanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    // The header row is drawn chrome (see Render); counts_ keeps its
    // right-aligned band on the same row.
    counts_->Layout({bounds.x + bounds.width - pad - 150.0f,
                     bounds.y + pad * 0.6f, 150.0f, 24.0f},
                    tokens);
    rows_top_ = bounds.y + 34.0f + tokens.spacing_small;
    rows_bottom_ = bounds.y + bounds.height - 30.0f;
    status_->Layout({bounds.x + pad, bounds.y + bounds.height - 26.0f,
                     bounds.width - pad * 2.0f, 20.0f},
                    tokens);
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());

    const int undo_count = static_cast<int>(undo_.size());
    const bool has_entries = !undo_.empty() || !redo_.empty();

    // rows_ is packed display order (undos then redos); display row
    // space inserts the divider between them.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const int display_row =
            static_cast<int>(i) + (static_cast<int>(i) >= undo_count ? 1 : 0);
        const Rect r = RowRect(display_row);
        Row& row = rows_[i];
        // Children of off-viewport rows hide — the panel clip covers
        // the title/status bands too, so a scrolled-away child would
        // otherwise paint (and hit-test) over the chrome.
        const bool visible =
            r.y + r.height > rows_top_ && r.y < rows_bottom_;
        row.item->Layout({r.x, r.y,
                          std::max(20.0f, r.width - 48.0f), r.height},
                         tokens);
        row.steps->Layout({r.Right() - 42.0f, r.y + 3.0f, 38.0f,
                           r.height - 6.0f},
                          tokens);
        row.item->SetVisible(visible);
        row.steps->SetVisible(visible);
    }

    // Divider row children sit at the undo/redo boundary.
    const Rect div = RowRect(undo_count);
    const bool div_visible =
        has_entries &&
        div.y + div.height > rows_top_ && div.y < rows_bottom_;
    const float caption_w = 116.0f;
    const float rule_w =
        std::max(0.0f, (div.width - caption_w - VeTheme::kSpaceSm * 2.0f) *
                           0.5f);
    divider_left_->Layout(
        {div.x + VeTheme::kSpaceSm,
         div.y + div.height * 0.5f - 0.5f, rule_w, 1.0f},
        tokens);
    divider_label_->Layout(
        {div.x + VeTheme::kSpaceSm + rule_w, div.y, caption_w,
         div.height},
        tokens);
    divider_right_->Layout(
        {div.x + VeTheme::kSpaceSm + rule_w + caption_w,
         div.y + div.height * 0.5f - 0.5f, rule_w, 1.0f},
        tokens);
    divider_left_->SetVisible(div_visible);
    divider_label_->SetVisible(div_visible);
    divider_right_->SetVisible(div_visible);
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
            RequestLayout();   // row children track the scroll offset
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
