#include "slip_tool.h"

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

bool SlipTool::OnPointerDown(const ToolContext& ctx,
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
    down_time_ = ctx.time;
    orig_start_ = ctx.clip->timeline_start;
    orig_in_ = ctx.clip->in_point;
    orig_duration_ = ctx.clip->duration;
    orig_rate_ = ctx.clip->playback_rate;
    delta_ = 0.0;
    // The clip keeps its span while slipping — the accent preview
    // outline marks it as the dragged clip.
    commit.SetClipPreview(clip_id_, orig_start_,
                          orig_start_ + orig_duration_);
    commit.RequestRepaint();
    return true;
}

bool SlipTool::OnPointerMove(const ToolContext& ctx,
                             ToolCommit& commit) {
    if (!dragging_ || ctx.project == nullptr) return false;
    if (FindClip(*ctx.project, clip_id_) == nullptr) {
        Cancel(commit);
        return true;
    }
    // Timeline drag distance maps into SOURCE seconds by playback_rate
    // (in_point is source-time — the SlipClip contract).
    delta_ = (ctx.time - down_time_) * orig_rate_;
    // Clamp into the plausible source window; the media bound is
    // re-validated by the model at commit.
    delta_ = std::max(delta_, -orig_in_);
    const double new_in = orig_in_ + delta_;
    const double new_out = new_in + orig_duration_ * orig_rate_;
    commit.SetStatus("Slip " + clip_id_ + "  in " +
                         FormatClock(new_in) + "  out " +
                         FormatClock(new_out),
                     false);
    commit.RequestRepaint();
    return true;
}

bool SlipTool::OnPointerUp(const ToolContext& ctx,
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
        commit.SlipClip(id, delta);
    }
    return true;
}

void SlipTool::Cancel(ToolCommit& commit) {
    clip_id_.clear();
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
