// Export queue panel: Media-Encoder-style render queue — one drawn
// row per job.
//
//   EXPORT QUEUE
//   ┌────────────────────────────────────────────────┐
//   │ interview.mp4                    [Queued]      │
//   │ 00:00-00:42 · H.264 8 Mbps      ▓▓▓░░░░░░  [x] │
//   ├────────────────────────────────────────────────┤
//   │ promo.mov                        [Running]     │
//   │ work area · ProRes 422        ▓▓▓▓▓▓░░░░  [x] │
//   ├────────────────────────────────────────────────┤
//   │ teaser.mp4                        [Done] [>>]  │
//   │ whole sequence · H.265 12 Mbps ▓▓▓▓▓▓▓▓▓▓     │
//   └────────────────────────────────────────────────┘
//
// Row text (basename `label`, `detail` = range/codec/bitrate line the
// host composes), the progress bar, and the state chip are drawn —
// not child widgets — while the per-row action buttons are Ghost
// Button children (same split as MarkersPanelWidget): [x] cancel for
// Queued/Running (and dismissing Failed rows), [>>] reveal for Done,
// [R] retry for Failed. Buttons fire hooks with the ROW INDEX into
// the last SetJobs() vector — the host maps that back to its own job
// ids. The list scrolls internally with the mouse wheel when it
// exceeds the viewport.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Button;
class Label;
}

namespace XER::VideoEditor::Ui {

// Values for ExportJobRow::state.
enum class ExportJobState : int {
    Queued = 0,
    Running = 1,
    Done = 2,
    Failed = 3,
};

struct ExportJobRow {
    std::string label;     // output basename ("interview.mp4")
    std::string detail;    // "range · codec bitrate" line (host-composed)
    float progress = 0.0f; // 0..1, shown on the bar (clamped)
    int state = 0;         // ExportJobState
};

class ExportQueuePanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // `job` is the row index into the last SetJobs() vector.
        std::function<void(int job)> on_cancel;
        std::function<void(int job)> on_reveal;   // open output folder
        std::function<void(int job)> on_retry;    // requeue a Failed row
        std::function<void(const std::string&, bool)> on_status;
    };

    explicit ExportQueuePanelWidget(Hooks hooks = {});

    // Replaces the whole queue display — call again on every engine
    // state/progress change. Progress-only updates may reuse the same
    // vector; the widget copies it.
    void SetJobs(const std::vector<ExportJobRow>& jobs);
    int JobCount() const { return static_cast<int>(jobs_.size()); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {260.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {420.0f, 300.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct Row {
        // All three always exist; Layout/visibility picks which show.
        std::shared_ptr<XER::UI::Widgets::Button> cancel;
        std::shared_ptr<XER::UI::Widgets::Button> reveal;
        std::shared_ptr<XER::UI::Widgets::Button> retry;
    };

    void EnsureChildren();
    void RebuildRows();
    int RowAt(const XER::UI::Layout::Point& position) const;
    XER::UI::Layout::Rect RowRect(int row) const;
    XER::UI::Layout::Rect ProgressRect(int row) const;
    float ScrollRange() const;
    void SetStatus(const std::string& text, bool is_error = false);

    Hooks hooks_;
    std::vector<ExportJobRow> jobs_;   // copy of the host's queue
    int hovered_ = -1;
    float scroll_y_ = 0.0f;
    float rows_top_ = 0.0f;            // layout: y where the list begins
    float rows_bottom_ = 0.0f;         // layout: list viewport bottom
    float row_height_ = 56.0f;
    bool children_attached_ = false;

    std::vector<Row> rows_;
    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> status_;
};

}  // namespace XER::VideoEditor::Ui
