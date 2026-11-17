#include "export_queue_panel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "rendering/renderer.h"
#include "theme/tokens.h"
#include "widgets/button.h"
#include "widgets/label.h"

namespace XER::VideoEditor::Ui {
namespace {

using XER::UI::Layout::Rect;
using XER::UI::Theme::Color;

Color WithAlpha(Color color, float alpha) {
    color.a *= alpha;
    return color;
}

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

// Chip fill/text per state — token-driven so the panel follows the
// theme (Queued/Running read as neutral/active, Done as accent,
// Failed as destructive).
void StateColors(ExportJobState state,
                 const XER::UI::Theme::Tokens& tokens,
                 Color* fill, Color* text) {
    switch (state) {
        case ExportJobState::Queued:
            *fill = tokens.secondary;
            *text = tokens.muted;
            return;
        case ExportJobState::Running:
            *fill = WithAlpha(tokens.accent, 0.35f);
            *text = tokens.accent;
            return;
        case ExportJobState::Done:
            *fill = tokens.accent;
            *text = tokens.accent_foreground;
            return;
        case ExportJobState::Failed:
            *fill = tokens.danger;
            *text = tokens.danger_foreground;
            return;
    }
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
        // Buttons carry per-row state (visibility by job state), so a
        // count change rebuilds; same-count updates only retoggle.
        if (!same_count) {
            RebuildRows();
        } else {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const ExportJobState s = StateOf(jobs_[i]);
                Row& row = rows_[i];
                row.cancel->SetVisible(s == ExportJobState::Queued ||
                                       s == ExportJobState::Running ||
                                       s == ExportJobState::Failed);
                row.reveal->SetVisible(s == ExportJobState::Done);
                row.retry->SetVisible(s == ExportJobState::Failed);
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
    for (Row& row : rows_) {
        RemoveChild(row.cancel);
        RemoveChild(row.reveal);
        RemoveChild(row.retry);
    }
    rows_.clear();
    rows_.reserve(jobs_.size());
    for (std::size_t i = 0; i < jobs_.size(); ++i) {
        const int job = static_cast<int>(i);
        const ExportJobState s = StateOf(jobs_[i]);
        Row row;
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
        row.cancel->SetVisible(s == ExportJobState::Queued ||
                               s == ExportJobState::Running ||
                               s == ExportJobState::Failed);
        row.reveal->SetVisible(s == ExportJobState::Done);
        row.retry->SetVisible(s == ExportJobState::Failed);
        AddChild(row.cancel);
        AddChild(row.reveal);
        AddChild(row.retry);
        rows_.push_back(std::move(row));
    }
    RequestLayout();
}

Rect ExportQueuePanelWidget::RowRect(int row) const {
    const auto bounds = GetBounds();
    return {bounds.x + 8.0f,
            rows_top_ + static_cast<float>(row) * row_height_ - scroll_y_,
            bounds.width - 16.0f, row_height_ - 4.0f};
}

Rect ExportQueuePanelWidget::ProgressRect(int row) const {
    const Rect r = RowRect(row);
    // Below the detail line; ends before the right-side button column
    // (and its trailing % readout) so nothing draws under a button.
    return {r.x + 4.0f, r.y + r.height - 13.0f,
            std::max(20.0f, r.width - 84.0f), 4.0f};
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

    // Text/progress/chip are drawn (not Label children) so row clicks
    // hit the panel — Click events do not bubble up from children.
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

        // Line 1: basename left, state chip just left of the button
        // column (buttons live at row.Right()-62 .. -6).
        renderer.DrawText(job.label,
                          {row.x + 8.0f, row.y + 5.0f,
                           std::max(30.0f, row.width - 140.0f), 18.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.foreground, tokens.font_body, 600});
        Color chip_fill, chip_text;
        StateColors(state, tokens, &chip_fill, &chip_text);
        const char* chip = StateLabel(state);
        const float chip_w = 58.0f;
        const Rect chip_rect{row.Right() - 70.0f - chip_w, row.y + 6.0f,
                             chip_w, 16.0f};
        renderer.FillRect(chip_rect, chip_fill, tokens.radius_small);
        renderer.DrawText(chip, chip_rect,
                          XER::UI::Rendering::TextStyle{
                              chip_text, tokens.font_small, 600});

        // Line 2: range + codec/bitrate detail (muted).
        renderer.DrawText(job.detail,
                          {row.x + 8.0f, row.y + 25.0f,
                           std::max(30.0f, row.width - 78.0f), 15.0f},
                          XER::UI::Rendering::TextStyle{
                              tokens.muted, tokens.font_small, 400});

        // Progress bar: recessed track + accent fill (danger tint when
        // the job failed so the bar doesn't read as completed work).
        const Rect bar = ProgressRect(static_cast<int>(i));
        renderer.FillRect(bar, tokens.input, bar.height * 0.5f);
        const float frac =
            std::clamp(std::isfinite(job.progress) ? job.progress : 0.0f,
                       0.0f, 1.0f);
        if (frac > 0.0f) {
            const Color fill = state == ExportJobState::Failed
                                   ? tokens.danger
                                   : tokens.accent;
            renderer.FillRect(
                {bar.x, bar.y, bar.width * frac, bar.height}, fill,
                bar.height * 0.5f);
        }
        if (state == ExportJobState::Running) {
            char pct[16];
            std::snprintf(pct, sizeof(pct), "%d%%",
                          static_cast<int>(frac * 100.0f + 0.5f));
            renderer.DrawText(pct,
                              {bar.Right() - 40.0f, bar.y - 4.0f, 40.0f,
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
            RequestLayout();   // row buttons track the scroll offset
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
