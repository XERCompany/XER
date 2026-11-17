#include "slide_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace XER::VideoEditor::Ui {
namespace {

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

bool SlideTool::OnPointerDown(const ToolContext& ctx,
                              ToolCommit& commit) {
    if (ctx.clip == nullptr) {
        commit.SelectClip("");
        return ctx.track >= 0;
    }
    commit.SelectClip(ctx.clip->id);
    if (ClipTrackLocked(*ctx.project, *ctx.clip)) {
        commit.SetStatus("track is locked", true);
        return true;
    }
    dragging_ = true;
    clip_id_ = ctx.clip->id;
    track_ = ctx.clip->track;
    orig_start_ = ctx.clip->timeline_start;
    orig_duration_ = ctx.clip->duration;
    grab_seconds_ = ctx.time - ctx.clip->timeline_start;
    delta_ = 0.0;
    return true;
}

bool SlideTool::OnPointerMove(const ToolContext& ctx,
                              ToolCommit& commit) {
    if (!dragging_ || ctx.project == nullptr) return false;
    if (FindClip(*ctx.project, clip_id_) == nullptr) {
        Cancel(commit);
        return true;
    }
    const double candidate = commit.SnapCandidate(
        std::max(0.0, ctx.time - grab_seconds_), track_);
    delta_ = candidate - orig_start_;
    commit.ClearClipPreviews();
    commit.SetClipPreview(clip_id_, orig_start_ + delta_,
                          orig_start_ + delta_ + orig_duration_);
    commit.SetStatus("Slide " + clip_id_ + " -> " +
                         FormatClock(orig_start_ + delta_),
                     false);
    commit.RequestRepaint();
    return true;
}

bool SlideTool::OnPointerUp(const ToolContext& ctx,
                            ToolCommit& commit) {
    (void)ctx;
    if (!dragging_) return false;
    const std::string id = clip_id_;
    const double delta = delta_;
    dragging_ = false;
    clip_id_.clear();
    commit.ClearClipPreviews();
    commit.RequestRepaint();
    if (std::fabs(delta) >= 1e-9) {
        commit.SlideClip(id, delta);
    }
    return true;
}

void SlideTool::Cancel(ToolCommit& commit) {
    clip_id_.clear();
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
