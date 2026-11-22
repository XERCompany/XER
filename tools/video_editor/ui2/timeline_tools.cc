// ui2 timeline edit tools — verbatim port of
// tools/video_editor/ui/tools/* into the Ui2 namespace: same gesture
// math, same commit ordering (linked co-edit members commit in the
// direction of travel, locked tracks refuse, trims clamp to the model
// floor), only EditTool -> TimelineTool and the commit interface names
// changed.

#include "timeline_tools.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace XER::VideoEditor::Ui2 {

using XER::UI::Widgets::MouseButton;

namespace {

constexpr double kMinClipDuration = 0.001;  // model floor, timeline s
constexpr double kModelEps = 1e-6;

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

// ---------- Selection (V): select, move, trim ----------

class SelectionTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::Selection; }
    const char* Name() const override { return "Selection"; }
    const char* CursorHint() const override {
        return "select / drag to move, edges to trim";
    }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
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
        // moves/trims by the same delta (the bridge wraps the
        // per-member commits in one undo macro). Skipped entirely
        // when the strip's "Linked Selection" toggle is off.
        members_.clear();
        if (!ctx.link_edits) return true;
        for (const Clip* member : LinkedClips(*ctx.project, *ctx.clip)) {
            if (member->id == clip_id_ ||
                ClipTrackLocked(*ctx.project, *member)) {
                continue;
            }
            members_.push_back(Member{member->id,
                                      member->timeline_start,
                                      member->in_point,
                                      member->duration,
                                      member->playback_rate});
        }
        return true;
    }

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (!dragging_ || ctx.project == nullptr) return false;
        const Clip* clip = FindClip(*ctx.project, clip_id_);
        if (clip == nullptr) {
            Cancel(commit);
            return true;
        }
        commit.ClearClipPreviews();
        if (drag_ == Drag::Move) {
            const double candidate = commit.SnapCandidate(
                std::max(0.0, ctx.time - grab_seconds_), track_);
            const double delta = candidate - orig_start_;
            preview_point_ = candidate;
            commit.SetClipPreview(clip_id_, orig_start_ + delta,
                                  orig_start_ + delta + orig_duration_);
            for (const Member& member : members_) {
                commit.SetClipPreview(member.id, member.start + delta,
                                      member.start + delta +
                                          member.duration);
            }
            commit.SetStatus("Move " + clip_id_ + " -> " +
                                 FormatClock(preview_point_),
                             false);
        } else {
            const double out_source =
                orig_in_ + orig_duration_ * orig_rate_;
            const double snapped =
                commit.SnapCandidate(ctx.time, track_);
            double point =
                orig_in_ + (snapped - orig_start_) * orig_rate_;
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
                                          member.start +
                                              member.duration);
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
                                          member.start +
                                              member.duration +
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

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
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
            // members never transiently overlap: moving right commits
            // the rightmost clip first (it vacates space for the
            // next), moving left the leftmost — the dragged clip is
            // part of the ordering, not appended last.
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
        // matching edge (source deltas scale by each member's own
        // rate).
        if (drag == Drag::TrimIn) {
            const double tl_delta = (point - orig_in) / orig_rate;
            if (std::fabs(tl_delta) < 1e-9) return true;
            for (const Member& member : members) {
                commit.TrimClip(member.id, TrimEdge::In,
                                member.in_point +
                                    tl_delta * member.rate);
            }
            commit.TrimClip(id, TrimEdge::In, point);
        } else {
            const double out_source =
                orig_in + orig_duration * orig_rate;
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

    void Cancel(ToolCommit& commit) override {
        drag_ = Drag::None;
        clip_id_.clear();
        members_.clear();
        TimelineTool::Cancel(commit);
    }

private:
    enum class Drag { None, Move, TrimIn, TrimOut };
    // One linked co-edit member (the drag clip keeps its own fields).
    struct Member {
        std::string id;
        double start = 0.0;
        double in_point = 0.0;
        double duration = 0.0;
        double rate = 1.0;
    };

    Drag drag_ = Drag::None;
    std::string clip_id_;
    int track_ = -1;
    double grab_seconds_ = 0.0;   // ctx.time - clip.start at pointer-down
    double orig_start_ = 0.0;
    double orig_in_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double preview_point_ = 0.0;  // last previewed source point / start
    std::vector<Member> members_;
};

// ---------- Razor (C) ----------

class RazorTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::Razor; }
    const char* Name() const override { return "Razor"; }
    const char* CursorHint() const override { return "cut"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (ctx.clip == nullptr) {
            // Clicks on empty track space / gaps are swallowed — the
            // razor never seeks or selects (that is the Selection
            // tool's job).
            return ctx.track >= 0;
        }
        if (ClipTrackLocked(*ctx.project, *ctx.clip)) {
            commit.SetStatus("track is locked", true);
            return true;
        }
        // The cut snaps like every other edit (markers, clip edges,
        // playhead, work-area edges — CollectSnapPoints).
        const double at =
            commit.SnapCandidate(ctx.time, ctx.clip->track);
        commit.SplitClip(ctx.clip->id, at);
        return true;
    }
};

// ---------- RippleEdit (B): edge trim + close/open the gap ----------

class RippleEditTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::RippleEdit; }
    const char* Name() const override { return "RippleEdit"; }
    const char* CursorHint() const override { return "ripple"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
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

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
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
            point = std::clamp(
                point, 0.0, out_source - kMinClipDuration * orig_rate_);
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

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
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
        // One atomic commit — the bridge closes the freed gap or
        // pushes the followers inside the same history entry
        // (plain-trim fallback lives inside ToolCommit::RippleTrim).
        commit.RippleTrim(
            id, drag == Drag::TrimIn ? TrimEdge::In : TrimEdge::Out,
            point);
        return true;
    }

    void Cancel(ToolCommit& commit) override {
        drag_ = Drag::None;
        clip_id_.clear();
        TimelineTool::Cancel(commit);
    }

private:
    enum class Drag { None, TrimIn, TrimOut };
    Drag drag_ = Drag::None;
    std::string clip_id_;
    int track_ = -1;
    double orig_start_ = 0.0;
    double orig_in_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double preview_point_ = 0.0;
};

// ---------- RollingEdit (N): shared cut — from-out / to-in ----------

class RollingEditTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::RollingEdit; }
    const char* Name() const override { return "RollingEdit"; }
    const char* CursorHint() const override { return "roll"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (ctx.clip == nullptr) {
            commit.SelectClip("");
            return ctx.track >= 0;
        }
        if (ctx.roll_from == nullptr || ctx.roll_to == nullptr) {
            commit.SelectClip(ctx.clip->id);
            commit.SetStatus(
                "Rolling Edit: drag a shared cut between adjacent "
                "clips",
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
        to_end_ = ctx.roll_to->timeline_start + ctx.roll_to->duration;
        from_duration_ = ctx.roll_from->duration;
        to_duration_ = ctx.roll_to->duration;
        delta_ = 0.0;
        commit.SelectClip(from_id_);
        return true;
    }

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (!dragging_ || ctx.project == nullptr) return false;
        if (FindClip(*ctx.project, from_id_) == nullptr ||
            FindClip(*ctx.project, to_id_) == nullptr) {
            Cancel(commit);
            return true;
        }
        const double snapped = commit.SnapCandidate(ctx.time, track_);
        // Clamp so neither side collapses below the model's duration
        // floor; media-handle bounds are enforced by the model at
        // commit.
        const double min_delta = kMinClipDuration - from_duration_;
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

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
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

    void Cancel(ToolCommit& commit) override {
        from_id_.clear();
        to_id_.clear();
        TimelineTool::Cancel(commit);
    }

private:
    std::string from_id_;
    std::string to_id_;
    int track_ = -1;
    double cut_ = 0.0;            // original cut position (timeline s)
    double from_start_ = 0.0;
    double to_end_ = 0.0;
    double from_duration_ = 0.0;  // clamps below keep both sides valid
    double to_duration_ = 0.0;
    double delta_ = 0.0;
};

// ---------- Slip (Y): shift the clip's source window ----------

class SlipTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::Slip; }
    const char* Name() const override { return "Slip"; }
    const char* CursorHint() const override { return "slip"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
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

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (!dragging_ || ctx.project == nullptr) return false;
        if (FindClip(*ctx.project, clip_id_) == nullptr) {
            Cancel(commit);
            return true;
        }
        // Timeline drag distance maps into SOURCE seconds by
        // playback_rate (in_point is source-time — the SlipClip
        // contract).
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

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
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

    void Cancel(ToolCommit& commit) override {
        clip_id_.clear();
        TimelineTool::Cancel(commit);
    }

private:
    std::string clip_id_;
    double down_time_ = 0.0;      // ctx.time at pointer-down
    double orig_in_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double orig_start_ = 0.0;
    double delta_ = 0.0;          // accumulated source-seconds delta
};

// ---------- Slide (U): move clip, neighbours absorb ----------

class SlideTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::Slide; }
    const char* Name() const override { return "Slide"; }
    const char* CursorHint() const override { return "slide"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
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

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
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

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
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

    void Cancel(ToolCommit& commit) override {
        clip_id_.clear();
        TimelineTool::Cancel(commit);
    }

private:
    std::string clip_id_;
    int track_ = -1;
    double orig_start_ = 0.0;
    double orig_duration_ = 0.0;
    double grab_seconds_ = 0.0;   // ctx.time - clip.start at pointer-down
    double delta_ = 0.0;
};

// ---------- RateStretch (R): edge drag retimes the clip ----------

class RateStretchTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::RateStretch; }
    const char* Name() const override { return "RateStretch"; }
    const char* CursorHint() const override { return "rate"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
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

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
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
            new_span = std::max(kMinClipDuration,
                                snapped - orig_start_);
        } else {
            const double end = orig_start_ + orig_duration_;
            new_span = std::max(kMinClipDuration, end - snapped);
        }
        constexpr double kMinRate = 0.02;   // preview clamp only —
        constexpr double kMaxRate = 100.0;  // the model re-validates
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

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
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

    void Cancel(ToolCommit& commit) override {
        drag_ = Drag::None;
        clip_id_.clear();
        TimelineTool::Cancel(commit);
    }

private:
    enum class Drag { None, EdgeIn, EdgeOut };
    Drag drag_ = Drag::None;
    std::string clip_id_;
    int track_ = -1;
    double orig_start_ = 0.0;
    double orig_duration_ = 0.0;
    double orig_rate_ = 1.0;
    double preview_rate_ = 1.0;
};

// ---------- Hand (H): pan the view ----------

class HandTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::Hand; }
    const char* Name() const override { return "Hand"; }
    const char* CursorHint() const override { return "hand"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
        (void)commit;
        dragging_ = true;
        anchor_time_ = ctx.time;
        return true;
    }

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (!dragging_) return false;
        // Keep the grabbed time under the pointer: ctx.time already
        // reflects the current scroll, so the correction is the
        // residual gap (the gesture converges in a single step).
        commit.PanBy(anchor_time_ - ctx.time);
        return true;
    }

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
        (void)ctx;
        (void)commit;
        if (!dragging_) return false;
        dragging_ = false;
        return true;
    }

private:
    double anchor_time_ = 0.0;    // timeline seconds under the pointer
};

// ---------- Zoom (Z): click = step, drag = range ----------

class ZoomTool final : public TimelineTool {
public:
    ToolId Id() const override { return ToolId::Zoom; }
    const char* Name() const override { return "Zoom"; }
    const char* CursorHint() const override { return "zoom"; }

    bool OnPointerDown(const ToolContext& ctx,
                       ToolCommit& commit) override {
        (void)commit;
        dragging_ = true;
        marquee_ = false;
        alt_ = ctx.is_alt;
        anchor_x_ = ctx.position.x;
        anchor_time_ = ctx.time;
        return true;
    }

    bool OnPointerMove(const ToolContext& ctx,
                       ToolCommit& commit) override {
        if (!dragging_) return false;
        constexpr float kMarqueeThresholdPx = 5.0f;
        if (!marquee_ &&
            std::fabs(ctx.position.x - anchor_x_) >=
                kMarqueeThresholdPx) {
            marquee_ = true;
        }
        if (marquee_) {
            commit.SetMarquee(true, std::min(anchor_x_, ctx.position.x),
                              std::max(anchor_x_, ctx.position.x));
            commit.RequestRepaint();
        }
        return true;
    }

    bool OnPointerUp(const ToolContext& ctx,
                     ToolCommit& commit) override {
        if (!dragging_) return false;
        const bool was_marquee = marquee_;
        dragging_ = false;
        marquee_ = false;
        commit.SetMarquee(false, 0.0f, 0.0f);
        if (was_marquee) {
            const double t0 = std::min(anchor_time_, ctx.time);
            const double t1 = std::max(anchor_time_, ctx.time);
            if (t1 - t0 > 1e-6) {
                commit.ZoomToRange(t0, t1);
            }
        } else {
            constexpr double kClickZoomFactor = 1.5;
            commit.ZoomAt(
                alt_ ? 1.0 / kClickZoomFactor : kClickZoomFactor,
                anchor_time_);
        }
        commit.RequestRepaint();
        return true;
    }

    void Cancel(ToolCommit& commit) override {
        marquee_ = false;
        TimelineTool::Cancel(commit);
    }

private:
    float anchor_x_ = 0.0f;
    double anchor_time_ = 0.0;
    bool marquee_ = false;
    bool alt_ = false;
};

}  // namespace

ToolId ToolIdForIndex(int index) {
    static const ToolId kOrder[kToolCount] = {
        ToolId::Selection,   ToolId::Razor,       ToolId::RippleEdit,
        ToolId::RollingEdit, ToolId::Slip,        ToolId::Slide,
        ToolId::RateStretch, ToolId::Hand,        ToolId::Zoom,
    };
    if (index < 0 || index >= kToolCount) return ToolId::Selection;
    return kOrder[index];
}

int ToolIndexFor(ToolId id) {
    for (int i = 0; i < kToolCount; ++i) {
        if (ToolIdForIndex(i) == id) return i;
    }
    return 0;
}

bool ClipTrackLocked(const Project& project, const Clip& clip) {
    if (clip.track < 0 ||
        clip.track >= static_cast<int>(project.tracks.size())) {
        return false;
    }
    return project.tracks[clip.track].locked;
}

std::vector<const Clip*> LinkedClips(const Project& project,
                                     const Clip& clip) {
    std::vector<const Clip*> members;
    if (clip.link_group.empty()) {
        members.push_back(&clip);
        return members;
    }
    for (const auto& member : project.clips) {
        if (member.link_group == clip.link_group) {
            members.push_back(&member);
        }
    }
    return members;
}

const Clip* NextClipOnTrack(const Project& project, int track,
                            double after_seconds) {
    const Clip* next = nullptr;
    if (track < 0 || track >= static_cast<int>(project.tracks.size())) {
        return nullptr;
    }
    for (const auto& clip : project.clips) {
        if (clip.track != track) continue;
        if (clip.timeline_start >= after_seconds - kModelEps &&
            (next == nullptr ||
             clip.timeline_start < next->timeline_start)) {
            next = &clip;
        }
    }
    return next;
}

const Clip* PrevClipOnTrack(const Project& project, int track,
                            double before_seconds) {
    const Clip* prev = nullptr;
    if (track < 0 || track >= static_cast<int>(project.tracks.size())) {
        return nullptr;
    }
    for (const auto& clip : project.clips) {
        if (clip.track != track) continue;
        const double end = clip.timeline_start + clip.duration;
        if (end <= before_seconds + kModelEps &&
            (prev == nullptr ||
             clip.timeline_start > prev->timeline_start)) {
            prev = &clip;
        }
    }
    return prev;
}

std::unique_ptr<TimelineTool> MakeTimelineTool(ToolId id) {
    switch (id) {
        case ToolId::Selection:   return std::make_unique<SelectionTool>();
        case ToolId::Razor:       return std::make_unique<RazorTool>();
        case ToolId::RippleEdit:  return std::make_unique<RippleEditTool>();
        case ToolId::RollingEdit: return std::make_unique<RollingEditTool>();
        case ToolId::Slip:        return std::make_unique<SlipTool>();
        case ToolId::Slide:       return std::make_unique<SlideTool>();
        case ToolId::RateStretch: return std::make_unique<RateStretchTool>();
        case ToolId::Hand:        return std::make_unique<HandTool>();
        case ToolId::Zoom:        return std::make_unique<ZoomTool>();
    }
    return std::make_unique<SelectionTool>();
}

}  // namespace XER::VideoEditor::Ui2
