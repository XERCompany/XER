#include "ripple_edit_tool.h"

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

bool RippleEditTool::OnPointerDown(const ToolContext& ctx,
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
    if (ctx.clip_part != ClipPart::EdgeIn &&
        ctx.clip_part != ClipPart::EdgeOut) {
        commit.SetStatus("Ripple Edit: drag a clip edge", false);
        return true;
    }
    dragging_ = true;
    drag_ = ctx.clip_part == ClipPart::EdgeIn ? Drag::TrimIn
                                            : Drag::TrimOut;
    clip_id_ = ctx.clip->id;
    track_ = ctx.clip->track;
    orig_start_ = ctx.clip->timeline_start;
    orig_in_ = ctx.clip->in_point;
    orig_duration_ = ctx.clip->duration;
    orig_rate_ = ctx.clip->playback_rate;
    preview_point_ = drag_ == Drag::TrimIn
        ? orig_in_
        : orig_in_ + orig_duration_ * orig_rate_;
    return true;
}

bool RippleEditTool::OnPointerMove(const ToolContext& ctx,
                                   ToolCommit& commit) {
    if (!dragging_ || ctx.project == nullptr) return false;
    const Clip* clip = FindClip(*ctx.project, clip_id_);
    if (clip == nullptr) {
        Cancel(commit);
        return true;
    }
    const double out_source = orig_in_ + orig_duration_ * orig_rate_;
    const double snapped = commit.SnapCandidate(ctx.time, track_);
    double point = orig_in_ + (snapped - orig_start_) * orig_rate_;
    commit.ClearClipPreviews();
    if (drag_ == Drag::TrimIn) {
        point = std::clamp(point, 0.0,
                           out_source - kMinClipDuration * orig_rate_);
        const double tl_delta = (point - orig_in_) / orig_rate_;
        commit.SetClipPreview(clip_id_, orig_start_ + tl_delta,
                              orig_start_ + orig_duration_);
    } else {
        point = std::max(point,
                         orig_in_ + kMinClipDuration * orig_rate_);
        const double new_end =
            orig_start_ + (point - orig_in_) / orig_rate_;
        commit.SetClipPreview(clip_id_, orig_start_, new_end);
    }
    preview_point_ = point;
    commit.SetStatus("Ripple trim " + clip_id_ + " -> " +
                         FormatClock(snapped),
                     false);
    commit.RequestRepaint();
    return true;
}

bool RippleEditTool::OnPointerUp(const ToolContext& ctx,
                                 ToolCommit& commit) {
    (void)ctx;
    if (!dragging_) return false;
    const Drag drag = drag_;
    const std::string id = clip_id_;
    const double point = preview_point_;
    dragging_ = false;
    drag_ = Drag::None;
    clip_id_.clear();
    commit.ClearClipPreviews();
    commit.RequestRepaint();
    // One atomic commit — the app closes the freed gap or pushes the
    // followers inside the same history entry (plain-trim fallback
    // lives inside ToolCommit::RippleTrim).
    commit.RippleTrim(
        id, drag == Drag::TrimIn ? TrimEdge::In : TrimEdge::Out, point);
    return true;
}

void RippleEditTool::Cancel(ToolCommit& commit) {
    drag_ = Drag::None;
    clip_id_.clear();
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
