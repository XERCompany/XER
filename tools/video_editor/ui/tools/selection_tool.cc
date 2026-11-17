#include "selection_tool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

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

bool SelectionTool::OnPointerDown(const ToolContext& ctx,
                                  ToolCommit& commit) {
    if (ctx.clip == nullptr) {
        commit.SelectClip("");
        return true;
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
    orig_in_ = ctx.clip->in_point;
    orig_duration_ = ctx.clip->duration;
    orig_rate_ = ctx.clip->playback_rate;
    drag_ = ctx.clip_part == ClipPart::EdgeIn    ? Drag::TrimIn
            : ctx.clip_part == ClipPart::EdgeOut ? Drag::TrimOut
                                                 : Drag::Move;
    grab_seconds_ = ctx.time - ctx.clip->timeline_start;
    preview_point_ = drag_ == Drag::Move
        ? orig_start_
        : drag_ == Drag::TrimIn
              ? orig_in_
              : orig_in_ + orig_duration_ * orig_rate_;

    // Linked co-edit snapshot: every other clip in the link_group
    // moves/trims by the same delta (the app wraps the per-member
    // commits in one undo macro).
    members_.clear();
    for (const Clip* member : LinkedClips(*ctx.project, *ctx.clip)) {
        if (member->id == clip_id_ ||
            ClipTrackLocked(*ctx.project, *member)) {
            continue;
        }
        members_.push_back(Member{member->id, member->timeline_start,
                                  member->in_point, member->duration,
                                  member->playback_rate});
    }
    return true;
}

bool SelectionTool::OnPointerMove(const ToolContext& ctx,
                                  ToolCommit& commit) {
    if (!dragging_ || ctx.project == nullptr) return false;
    const Clip* clip = FindClip(*ctx.project, clip_id_);
    if (clip == nullptr) {
        Cancel(commit);
        return true;
    }
    commit.ClearClipPreviews();
    if (drag_ == Drag::Move) {
        const double candidate =
            commit.SnapCandidate(std::max(0.0, ctx.time - grab_seconds_),
                                 track_);
        const double delta = candidate - orig_start_;
        preview_point_ = candidate;
        commit.SetClipPreview(clip_id_, orig_start_ + delta,
                              orig_start_ + delta + orig_duration_);
        for (const Member& member : members_) {
            commit.SetClipPreview(member.id, member.start + delta,
                                  member.start + delta + member.duration);
        }
        commit.SetStatus("Move " + clip_id_ + " -> " +
                             FormatClock(preview_point_),
                         false);
    } else {
        const double out_source =
            orig_in_ + orig_duration_ * orig_rate_;
        const double snapped =
            commit.SnapCandidate(ctx.time, track_);
        double point = orig_in_ + (snapped - orig_start_) * orig_rate_;
        if (drag_ == Drag::TrimIn) {
            point = std::clamp(
                point, 0.0,
                out_source - kMinClipDuration * orig_rate_);
            preview_point_ = point;
            const double tl_delta = (point - orig_in_) / orig_rate_;
            commit.SetClipPreview(clip_id_, orig_start_ + tl_delta,
                                  orig_start_ + orig_duration_);
            for (const Member& member : members_) {
                commit.SetClipPreview(member.id,
                                      member.start + tl_delta,
                                      member.start + member.duration);
            }
        } else {
            point = std::max(
                point, orig_in_ + kMinClipDuration * orig_rate_);
            preview_point_ = point;
            const double tl_delta =
                (point - out_source) / orig_rate_;
            commit.SetClipPreview(clip_id_, orig_start_,
                                  orig_start_ + orig_duration_ +
                                      tl_delta);
            for (const Member& member : members_) {
                commit.SetClipPreview(member.id, member.start,
                                      member.start + member.duration +
                                          tl_delta);
            }
        }
        commit.SetStatus("Trim " + clip_id_ + " -> " +
                             FormatClock(snapped),
                         false);
    }
    commit.RequestRepaint();
    return true;
}

bool SelectionTool::OnPointerUp(const ToolContext& ctx,
                                ToolCommit& commit) {
    (void)ctx;
    if (!dragging_) return false;
    const Drag drag = drag_;
    const std::string id = clip_id_;
    const double point = preview_point_;
    const std::vector<Member> members = members_;
    const double orig_start = orig_start_;
    const double orig_in = orig_in_;
    const double orig_rate = orig_rate_;
    const double orig_duration = orig_duration_;
    dragging_ = false;
    drag_ = Drag::None;
    clip_id_.clear();
    members_.clear();
    commit.ClearClipPreviews();
    commit.RequestRepaint();

    if (drag == Drag::Move) {
        const double delta = point - orig_start;
        if (std::fabs(delta) < 1e-9) return true;
        // Commit in the direction of travel so same-track linked
        // members never transiently overlap: moving right commits the
        // rightmost clip first (it vacates space for the next),
        // moving left the leftmost — the dragged clip is part of the
        // ordering, not appended last.
        std::vector<std::pair<double, std::pair<std::string, double>>>
            ordered;
        ordered.reserve(members.size() + 1);
        ordered.emplace_back(orig_start,
                             std::make_pair(id, point));
        for (const Member& member : members) {
            ordered.emplace_back(
                member.start,
                std::make_pair(member.id, member.start + delta));
        }
        std::sort(ordered.begin(), ordered.end(),
                  [delta](const auto& a, const auto& b) {
                      return delta > 0.0 ? a.first > b.first
                                         : a.first < b.first;
                  });
        for (const auto& entry : ordered) {
            commit.MoveClip(entry.second.first, entry.second.second);
        }
        return true;
    }
    // Trim: apply the same TIMELINE delta to every linked member's
    // matching edge (source deltas scale by each member's own rate).
    if (drag == Drag::TrimIn) {
        const double tl_delta = (point - orig_in) / orig_rate;
        if (std::fabs(tl_delta) < 1e-9) return true;
        for (const Member& member : members) {
            commit.TrimClip(member.id, TrimEdge::In,
                            member.in_point + tl_delta * member.rate);
        }
        commit.TrimClip(id, TrimEdge::In, point);
    } else {
        const double out_source = orig_in + orig_duration * orig_rate;
        const double tl_delta = (point - out_source) / orig_rate;
        if (std::fabs(tl_delta) < 1e-9) return true;
        for (const Member& member : members) {
            commit.TrimClip(member.id, TrimEdge::Out,
                            member.in_point +
                                member.duration * member.rate +
                                tl_delta * member.rate);
        }
        commit.TrimClip(id, TrimEdge::Out, point);
    }
    return true;
}

void SelectionTool::Cancel(ToolCommit& commit) {
    drag_ = Drag::None;
    clip_id_.clear();
    members_.clear();
    EditTool::Cancel(commit);
}

}  // namespace XER::VideoEditor::Ui
