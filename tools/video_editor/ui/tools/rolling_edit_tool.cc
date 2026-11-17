#include "rolling_edit_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace XER::VideoEditor::Ui {
namespace {

constexpr double kMinClipDuration = 0.001;  // model floor, timeline s

std::string FormatClock(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return "0:00";
    const int minutes = static_cast<int>(seconds / 60.0);
    const int whole = static_cast<int>(std::fmod(seconds, 60.0));
    const int millis = static_cast<int>(
        std::round((seconds - std::floor(seconds)) * 1000.0));
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%d:%02d.%03d", minutes, whole,
                  millis == 1000 ? 999 : millis);
    return buffer;
}

}  // namespace

bool RollingEditTool::OnPointerDown(const ToolContext& ctx,
                                    ToolCommit& commit) {
    if (ctx.clip == nullptr) {
        commit.SelectClip("");
        return ctx.track >= 0;
    }
    if (ctx.roll_from == nullptr || ctx.roll_to == nullptr) {
        commit.SelectClip(ctx.clip->id);
        commit.SetStatus(
            "Rolling Edit: drag a shared cut between adjacent clips",
            false);
        return true;
    }
    if (ClipTrackLocked(*ctx.project, *ctx.roll_from)) {
        commit.SetStatus("track is locked", true);
        return true;
    }
    dragging_ = true;
    from_id_ = ctx.roll_from->id;
    to_id_ = ctx.roll_to->id;
    track_ = ctx.roll_from->track;
    cut_ = ctx.roll_to->timeline_start;   // == ClipEndSeconds(from)
    from_start_ = ctx.roll_from->timeline_start;
    to_end_ = ClipEndSeconds(*ctx.roll_to);
    from_duration_ = ctx.roll_from->duration;
    to_duration_ = ctx.roll_to->duration;
    delta_ = 0.0;
    commit.SelectClip(from_id_);
    return true;
}

bool RollingEditTool::OnPointerMove(const ToolContext& ctx,
                                    ToolCommit& commit) {
    if (!dragging_ || ctx.project == nullptr) return false;
    if (FindClip(*ctx.project, from_id_) == nullptr ||
        FindClip(*ctx.project, to_id_) == nullptr) {
        Cancel(commit);
        return true;
    }
    const double snapped = commit.SnapCandidate(ctx.time, track_);
    // Clamp so neither side collapses below the model's duration
    // floor; media-handle bounds are enforced by the model at commit.
    const double min_delta =
        kMinClipDuration - from_duration_;
    const double max_delta = to_duration_ - kMinClipDuration;
    delta_ = std::clamp(snapped - cut_, min_delta,
                        std::max(min_delta, max_delta));
    commit.ClearClipPreviews();
    commit.SetClipPreview(from_id_, from_start_, cut_ + delta_);
    commit.SetClipPreview(to_id_, cut_ + delta_, to_end_);
    commit.SetStatus("Roll cut -> " + FormatClock(cut_ + delta_),
                     false);
    commit.RequestRepaint();
    return true;
}

bool RollingEditTool::OnPointerUp(const ToolContext& ctx,
                                  ToolCommit& commit) {
    (void)ctx;
    if (!dragging_) return false;
    const std::string from_id = from_id_;
    const std::string to_id = to_id_;
    const double delta = delta_;
    dragging_ = false;
    from_id_.clear();
    to_id_.clear();
    commit.ClearClipPreviews();
    commit.RequestRepaint();
    if (std::fabs(delta) >= 1e-9) {
        commit.RollingEdit(from_id, to_id, delta);
    }
    return true;
}

void RollingEditTool::Cancel(ToolCommit& commit) {
    from_id_.clear();
    to_id_.clear();
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
