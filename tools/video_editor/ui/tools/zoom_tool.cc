#include "zoom_tool.h"

#include <algorithm>
#include <cmath>

namespace XER::VideoEditor::Ui {
namespace {
constexpr float kMarqueeThresholdPx = 5.0f;
constexpr double kClickZoomFactor = 1.5;
}

bool ZoomTool::OnPointerDown(const ToolContext& ctx,
                             ToolCommit& commit) {
    (void)commit;
    dragging_ = true;
    marquee_ = false;
    alt_ = ctx.is_alt;
    anchor_x_ = ctx.position.x;
    anchor_time_ = ctx.time;
    return true;
}

bool ZoomTool::OnPointerMove(const ToolContext& ctx,
                             ToolCommit& commit) {
    if (!dragging_) return false;
    if (!marquee_ &&
        std::fabs(ctx.position.x - anchor_x_) >= kMarqueeThresholdPx) {
        marquee_ = true;
    }
    if (marquee_) {
        commit.SetMarquee(true, std::min(anchor_x_, ctx.position.x),
                          std::max(anchor_x_, ctx.position.x));
        commit.RequestRepaint();
    }
    return true;
}

bool ZoomTool::OnPointerUp(const ToolContext& ctx,
                           ToolCommit& commit) {
    if (!dragging_) return false;
    const bool was_marquee = marquee_;
    dragging_ = false;
    marquee_ = false;
    commit.SetMarquee(false, 0.0f, 0.0f);
    if (was_marquee) {
        const double t0 =
            std::min(anchor_time_, ctx.time);
        const double t1 =
            std::max(anchor_time_, ctx.time);
        if (t1 - t0 > 1e-6) {
            commit.ZoomToRange(t0, t1);
        }
    } else {
        commit.ZoomAt(alt_ ? 1.0 / kClickZoomFactor : kClickZoomFactor,
                      anchor_time_);
    }
    commit.RequestRepaint();
    return true;
}

void ZoomTool::Cancel(ToolCommit& commit) {
    marquee_ = false;
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
