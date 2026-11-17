#include "rate_stretch_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace XER::VideoEditor::Ui {
namespace {

constexpr double kMinClipDuration = 0.001;  // model floor, timeline s
constexpr double kMinRate = 0.02;           // preview clamp only —
constexpr double kMaxRate = 100.0;          // the model re-validates

}  // namespace

bool RateStretchTool::OnPointerDown(const ToolContext& ctx,
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
        commit.SetStatus("Rate Stretch: drag a clip edge", false);
        return true;
    }
    dragging_ = true;
    drag_ = ctx.clip_part == ClipPart::EdgeIn ? Drag::EdgeIn
                                            : Drag::EdgeOut;
    clip_id_ = ctx.clip->id;
    track_ = ctx.clip->track;
    orig_start_ = ctx.clip->timeline_start;
    orig_duration_ = ctx.clip->duration;
    orig_rate_ = ctx.clip->playback_rate;
    preview_rate_ = orig_rate_;
    return true;
}

bool RateStretchTool::OnPointerMove(const ToolContext& ctx,
                                    ToolCommit& commit) {
    if (!dragging_ || ctx.project == nullptr) return false;
    if (FindClip(*ctx.project, clip_id_) == nullptr) {
        Cancel(commit);
        return true;
    }
    // Premiere rate stretch: the consumed source span
    // (duration * rate) stays fixed, the timeline span flexes, and
    // rate = consumed / new_span. The anchored edge does not move.
    const double consumed = orig_duration_ * orig_rate_;
    const double snapped = commit.SnapCandidate(ctx.time, track_);
    double new_span;
    if (drag_ == Drag::EdgeOut) {
        new_span = std::max(kMinClipDuration, snapped - orig_start_);
    } else {
        const double end = orig_start_ + orig_duration_;
        new_span = std::max(kMinClipDuration, end - snapped);
    }
    preview_rate_ =
        std::clamp(consumed / new_span, kMinRate, kMaxRate);
    const double preview_span = consumed / preview_rate_;
    commit.ClearClipPreviews();
    if (drag_ == Drag::EdgeOut) {
        commit.SetClipPreview(clip_id_, orig_start_,
                              orig_start_ + preview_span);
    } else {
        const double end = orig_start_ + orig_duration_;
        commit.SetClipPreview(clip_id_, end - preview_span, end);
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "Rate %s %.2fx",
                  clip_id_.c_str(), preview_rate_);
    commit.SetStatus(buffer, false);
    commit.RequestRepaint();
    return true;
}

bool RateStretchTool::OnPointerUp(const ToolContext& ctx,
                                  ToolCommit& commit) {
    (void)ctx;
    if (!dragging_) return false;
    const std::string id = clip_id_;
    const double rate = preview_rate_;
    const double orig_rate = orig_rate_;
    dragging_ = false;
    drag_ = Drag::None;
    clip_id_.clear();
    commit.ClearClipPreviews();
    commit.RequestRepaint();
    if (std::fabs(rate - orig_rate) >= 1e-9) {
        commit.SetClipRate(id, rate);
    }
    return true;
}

void RateStretchTool::Cancel(ToolCommit& commit) {
    drag_ = Drag::None;
    clip_id_.clear();
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
