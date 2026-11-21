#include "export_queue_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "ve_theme.h"
#include "widgets/badge.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/progress.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;

ExportJobState StateOf(const ExportJobRow& job) {
    switch (static_cast<ExportJobState>(job.state)) {
        case ExportJobState::Queued:
        case ExportJobState::Running:
        case ExportJobState::Done:
        case ExportJobState::Failed:
            return static_cast<ExportJobState>(job.state);
    }
    return ExportJobState::Queued;
}

const char* StateLabel(ExportJobState state) {
    switch (state) {
        case ExportJobState::Queued:  return "Queued";
        case ExportJobState::Running: return "Running";
        case ExportJobState::Done:    return "Done";
        case ExportJobState::Failed:  return "Failed";
    }
    return "?";
}

// State -> BadgeVariant: Failed takes the danger Destructive chip,
// Running takes Accent (ties the chip to the accent progress fill =
// "work in progress"), Done settles into quiet Secondary, and Queued
// stays hollow Outline.
XER::UI::Widgets::BadgeVariant StateVariant(ExportJobState state) {
    switch (state) {
        case ExportJobState::Queued:
            return XER::UI::Widgets::BadgeVariant::Outline;
        case ExportJobState::Running:
            return XER::UI::Widgets::BadgeVariant::Accent;
        case ExportJobState::Done:
            return XER::UI::Widgets::BadgeVariant::Secondary;
        case ExportJobState::Failed:
            return XER::UI::Widgets::BadgeVariant::Destructive;
    }
    return XER::UI::Widgets::BadgeVariant::Outline;
}

}  // namespace

ExportQueuePanelWidget::ExportQueuePanelWidget(Hooks hooks)
    : hooks_(std::move(hooks)) {
    SetFocusable(true);
    SetClipsChildren(true);   // rows scroll inside the panel
}

void ExportQueuePanelWidget::EnsureChildren() {
    if (children_attached_) return;
    children_attached_ = true;
    namespace Widgets = XER::UI::Widgets;

    title_ = std::make_shared<Widgets::Label>("EXPORT QUEUE");
    status_ = std::make_shared<Widgets::Label>("queue is empty", true);
    AddChild(title_);
    AddChild(status_);
    RebuildRows();
}

void ExportQueuePanelWidget::SetJobs(
    const std::vector<ExportJobRow>& jobs) {
    const bool same_count = jobs_.size() == jobs.size();
    jobs_ = jobs;
    hovered_ = -1;
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());
    if (children_attached_) {
        // Children carry per-row state (badge variant, progress value,
        // action visibility), so a count change rebuilds; same-count
        // updates only re-sync.
        if (!same_count) {
            RebuildRows();
        } else {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                SyncRow(i);
            }
            RequestLayout();
        }
        int running = 0;
        for (const ExportJobRow& job : jobs_) {
            if (StateOf(job) == ExportJobState::Running) ++running;
        }
        status_->SetText(jobs_.empty()
                             ? "queue is empty"
                             : std::to_string(jobs_.size()) + " job(s)" +
                                   (running > 0 ? " · " +
                                        std::to_string(running) +
                                        " running"
                                                : ""));
    }
    Invalidate();
}

void ExportQueuePanelWidget::RebuildRows() {
    namespace Widgets = XER::UI::Widgets;
    // Sweep the previous generation's retirees (see retired_rows_ in
    // the header — RebuildRows can run inside a row button's OnClick).
    retired_rows_.clear();
    for (Row& row : rows_) {
        const std::shared_ptr<Widgets::Widget> children[] = {
            row.badge, row.progress, row.cancel, row.reveal, row.retry};
        for (const auto& child : children) {
            child->SetEnabled(false);
            RemoveChild(child);
            retired_rows_.push_back(child);
        }
    }
    rows_.clear();
    rows_.reserve(jobs_.size());
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const int job = static_cast<int>(i);
        Row row;
        row.badge = std::make_shared<Widgets::Badge>();
        row.progress = std::make_shared<Widgets::Progress>(0.0f);
        row.cancel = std::make_shared<Widgets::Button>(
            "x", Widgets::ButtonStyle::Ghost);
        row.cancel->SetTooltip("Cancel / remove job");
        row.cancel->SetOnClick([this, job] {
            if (hooks_.on_cancel) hooks_.on_cancel(job);
            SetStatus("cancel requested");
        });
        row.reveal = std::make_shared<Widgets::Button>(
            ">>", Widgets::ButtonStyle::Ghost);
        row.reveal->SetTooltip("Reveal output file");
        row.reveal->SetOnClick([this, job] {
            if (hooks_.on_reveal) hooks_.on_reveal(job);
        });
        row.retry = std::make_shared<Widgets::Button>(
            "R", Widgets::ButtonStyle::Ghost);
        row.retry->SetTooltip("Retry failed job");
        row.retry->SetOnClick([this, job] {
            if (hooks_.on_retry) hooks_.on_retry(job);
            SetStatus("job requeued");
        });
        AddChild(row.badge);
        AddChild(row.progress);
        AddChild(row.cancel);
        AddChild(row.reveal);
        AddChild(row.retry);
        rows_.push_back(std::move(row));
        SyncRow(i);
    }
    RequestLayout();
}

void ExportQueuePanelWidget::SyncRow(std::size_t index) {
    if (index >= rows_.size() || index >= jobs_.size()) return;
    const ExportJobRow& job = jobs_[index];
    const ExportJobState s = StateOf(job);
    Row& row = rows_[index];
    row.badge->SetText(StateLabel(s));
    row.badge->SetVariant(StateVariant(s));
    row.progress->SetValue(
        std::clamp(std::isfinite(job.progress) ? job.progress : 0.0f,
                   0.0f, 1.0f));
    row.cancel->SetVisible(s == ExportJobState::Queued ||
                           s == ExportJobState::Running ||
                           s == ExportJobState::Failed);
    row.reveal->SetVisible(s == ExportJobState::Done);
    row.retry->SetVisible(s == ExportJobState::Failed);
}

Rect ExportQueuePanelWidget::RowRect(int row) const {
    const auto bounds = GetBounds();
    return {bounds.x + VeTheme::kSpaceSm,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - VeTheme::kSpaceSm * 2.0f,
            row_height_ - 4.0f};
}

Rect ExportQueuePanelWidget::ProgressRect(int row) const {
    const Rect r = RowRect(row);
    // Below the detail line; ends before the right-side button column
    // (and its trailing % readout) so nothing draws under a button.
    return {r.x + 4.0f, r.y + r.height - 12.0f,
            std::max(20.0f, r.width - 84.0f), 6.0f};
}

int ExportQueuePanelWidget::RowAt(
    const XER::UI::Layout::Point& position) const {
    const auto bounds = GetBounds();
    if (position.y < rows_top_ || position.y >= rows_bottom_ ||
        position.x < bounds.x || position.x >= bounds.x + bounds.width) {
        return -1;
    }
    const int row = static_cast<int>(
        std::floor((position.y - rows_top_ + scroll_y_) / row_height_));
    return row >= 0 && row < static_cast<int>(jobs_.size()) ? row : -1;
}

float ExportQueuePanelWidget::ScrollRange() const {
    const float view_h =
        std::max(0.0f, rows_bottom_ - rows_top_);
    return std::max(
        0.0f, static_cast<float>(jobs_.size()) * row_height_ - view_h);
}

void ExportQueuePanelWidget::SetStatus(const std::string& text,
                                       bool is_error) {
    if (children_attached_) status_->SetText(text);
    if (hooks_.on_status) hooks_.on_status(text, is_error);
}

void ExportQueuePanelWidget::Render(
    XER::UI::Rendering::Renderer& renderer,
    const XER::UI::Theme::Tokens& tokens) {
    EnsureChildren();
    const auto bounds = GetBounds();
    renderer.FillRect(bounds, tokens.surface, tokens.radius_large);
    renderer.StrokeRect(bounds, tokens.border, 1.0f, tokens.radius_large);
    if (jobs_.empty() || rows_top_ <= 0.0f) {
        renderer.DrawText("exports queued from the export dialog land here",
                          {bounds.x + 12.0f,
                           std::max(rows_top_, bounds.y + 64.0f) + 6.0f,
                           bounds.width - 24.0f, 20.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});
        return;
    }

    // Chip and bar are Badge/Progress children now — the panel draws
    // the row wash, the two text lines, and the running % readout.
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        if (row.y + row.height < rows_top_ || row.y > rows_bottom_) {
            continue;
        }
        const ExportJobRow& job = jobs_[i];
        const ExportJobState state = StateOf(job);
        if (static_cast<int>(i) == hovered_) {
            renderer.FillRect(row, tokens.hover, tokens.radius_small);
        } else {
            renderer.FillRect(row, tokens.panel, tokens.radius_small);
        }

        // Line 1: basename left; the Badge child owns the chip slot
        // just left of the button column (row.Right()-134 .. -70).
        renderer.DrawText(job.label,
                          {row.x + 8.0f, row.y + 5.0f,
                           std::max(30.0f, row.width - 150.0f), 18.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.foreground, tokens.font_body, 600});

        // Line 2: range + codec/bitrate detail (muted).
        renderer.DrawText(job.detail,
                          {row.x + 8.0f, row.y + 25.0f,
                           std::max(30.0f, row.width - 78.0f), 15.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});

        // Running % readout at the progress bar's right edge (the bar
        // itself is the row's Progress child).
        if (state == ExportJobState::Running) {
            const Rect bar = ProgressRect(static_cast<int>(i));
            const float frac = std::clamp(
                std::isfinite(job.progress) ? job.progress : 0.0f,
                0.0f, 1.0f);
            char pct[16];
            std::snprintf(pct, sizeof(pct), "%d%%",
                          static_cast<int>(frac * 100.0f + 0.5f));
            renderer.DrawText(pct,
                              {bar.Right() - 40.0f, bar.y - 3.0f, 40.0f,
                               12.0f},
                              XER::UI::Rendering::TextStyle{
                                  tokens.muted, tokens.font_small, 400});
        }
    }
}

void ExportQueuePanelWidget::Layout(const XER::UI::Layout::Rect& bounds) {
    EnsureChildren();
    SetBounds(bounds);
    const auto& tokens = CachedLayoutTokens();
    const float pad = tokens.spacing_medium;

    title_->Layout({bounds.x + pad, bounds.y + pad * 0.6f,
                    bounds.width - pad * 2.0f, 24.0f},
                   tokens);
    rows_top_ = bounds.y + 34.0f + tokens.spacing_small;
    rows_bottom_ = bounds.y + bounds.height - 34.0f;
    scroll_y_ = std::clamp(scroll_y_, 0.0f, ScrollRange());

    for (std::size_t i = 0; i < rows_.size(); ++i) {
        const Rect row = RowRect(static_cast<int>(i));
        Row& r = rows_[i];
        // Hide children of rows scrolled outside the viewport — the
        // panel clip covers the title/status bands too, so without
        // this an off-viewport child would paint (and hit-test) over
        // the chrome.
        const bool visible =
            row.y + row.height > rows_top_ && row.y < rows_bottom_;
        // Badge chip just left of the button column.
        r.badge->Layout({row.Right() - 134.0f, row.y + 5.0f, 64.0f,
                         18.0f},
                        tokens);
        r.progress->Layout(ProgressRect(static_cast<int>(i)), tokens);
        // Button column at the row's right edge, vertically centered.
        r.cancel->Layout({row.Right() - 30.0f, row.y + 14.0f, 24.0f,
                          24.0f},
                         tokens);
        r.reveal->Layout({row.Right() - 34.0f, row.y + 14.0f, 28.0f,
                          24.0f},
                         tokens);
        r.retry->Layout({row.Right() - 62.0f, row.y + 14.0f, 28.0f,
                         24.0f},
                        tokens);
        r.badge->SetVisible(visible);
        r.progress->SetVisible(visible);
        // Action buttons keep their state-driven visibility — a hidden
        // (wrong-state) button stays hidden even in the viewport.
        if (!visible) {
            r.cancel->SetVisible(false);
            r.reveal->SetVisible(false);
            r.retry->SetVisible(false);
        } else {
            SyncRow(i);
        }
    }
    status_->Layout({bounds.x + pad,
                     bounds.y + bounds.height - 28.0f,
                     bounds.width - pad * 2.0f, 22.0f},
                    tokens);
}

bool ExportQueuePanelWidget::HandleEvent(
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
        case XER::UI::Widgets::Event::Type::Click:
            // Rows have no click action of their own — the buttons do.
            return RowAt(event.position) >= 0;
        default:
            return false;
    }
}

void ExportQueuePanelWidget::Tick(double dt_seconds) {
    (void)dt_seconds;
}

}  // namespace XER::VideoEditor::Ui
