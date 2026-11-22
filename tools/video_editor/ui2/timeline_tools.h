// ui2 timeline edit tools — Ui2 port of ui/tools/* (Premiere tool
// model, spec §6 S1). Exactly one tool is active at a time; the shell
// binds V/C/B/N/Y/U/R/H/Z through ShortcutRouter and calls
// TimelineSurface::SetTool; the surface resolves every pointer event
// into a ToolContext snapshot and delegates the gesture to the active
// TimelineTool.
//
// Tools never mutate the Project directly — every commit funnels
// through ToolCommit, which TimelineSurface implements on top of its
// TimelineHooks, so the shell stays the single owner of
// TimelineHistory (multi-clip gestures — linked-group moves, ripple
// trims — are wrapped in one undo macro by the bridge's gesture
// bracketing, see surface_hooks.h on_gesture_begin/end).
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "layout/geometry.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {

// Tool ids follow the Premiere keyboard mnemonics — the int order is
// the strip's button order (V=0 C=1 B=2 N=3 Y=4 U=5 R=6 H=7 Z=8).
enum class ToolId {
    Selection,    // V — select, move, trim (default)
    Razor,        // C — click splits the clip at the pointer
    RippleEdit,   // B — edge drag trims and closes the opened gap
    RollingEdit,  // N — drag an adjacent cut: from-out / to-in
    Slip,         // Y — body drag shifts the clip's source window
    Slide,        // U — body drag moves the clip, neighbours absorb
    RateStretch,  // R — edge drag retimes the clip (playback_rate)
    Hand,         // H — drag pans the view
    Zoom,         // Z — click zooms in, Alt-click out, drag = range
};
inline constexpr int kToolCount = 9;
// Strip order <-> ToolId (identity today — kept as a table so a
// reorder stays one-line).
ToolId ToolIdForIndex(int index);
int ToolIndexFor(ToolId id);

// Part of the clip body under the pointer. EdgeIn/EdgeOut use the same
// ~7 px grab zones the classic trim affordance draws at clip corners.
enum class ClipPart { None, Body, EdgeIn, EdgeOut };

// Per-event snapshot the surface hands to the active tool: a read-only
// project view plus the resolved hit (timeline seconds, track row,
// clip, clip part, and — for shared cuts — the adjacent pair a rolling
// edit would move). Clip pointers are only valid for the duration of
// the event: the project may be swapped between events after a commit,
// so tools keep clip ids and re-resolve through ctx.project.
struct ToolContext {
    const Project* project = nullptr;
    XER::UI::Layout::Point position{};
    double time = 0.0;                    // timeline seconds at position.x
    int track = -1;                       // row under position.y (-1 = none)
    const Clip* clip = nullptr;           // top clip under the pointer
    ClipPart clip_part = ClipPart::None;
    // Set only when clip_part is an edge shared by an adjacent pair on
    // the same track (roll_from ends where roll_to starts).
    const Clip* roll_from = nullptr;
    const Clip* roll_to = nullptr;
    double playhead = 0.0;
    double px_per_second = 72.0;
    XER::UI::Widgets::MouseButton button =
        XER::UI::Widgets::MouseButton::None;
    bool is_ctrl = false;
    bool is_shift = false;
    bool is_alt = false;
    // Strip's "Linked Selection" toggle: when false, Selection skips
    // the link_group co-edit snapshot (Premiere's toggle-off state).
    bool link_edits = true;
};

// Commit gateway implemented by the surface: forwards tool decisions
// to the TimelineHooks and lets the tool report ghost geometry,
// marquee state and status text. Every method maps to a TimelineHooks
// member except the view/preview controls, which stay surface-local.
class ToolCommit {
public:
    virtual ~ToolCommit() = default;

    virtual void SelectClip(const std::string& clip_id) = 0;
    virtual void SetStatus(const std::string& text, bool is_error) = 0;
    // Repaint request (named RequestRepaint, NOT Invalidate, so the
    // surface can implement this interface alongside
    // Widget::Invalidate without a name collision).
    virtual void RequestRepaint() = 0;

    // Snaps `seconds` to the model's snap candidates on `track` (clip
    // edges, marker times, work-area edges, playhead — see
    // CollectSnapPoints) when snapping is enabled; returns `seconds`
    // unchanged otherwise.
    virtual double SnapCandidate(double seconds, int track) const = 0;

    // Ghost geometry: while set, the surface draws the clip at
    // [start_seconds, end_seconds) (timeline seconds) with an accent
    // outline and dims its real span. Several clips may preview at
    // once (rolling edits, linked co-move); ClearClipPreviews clears
    // them all. Previewing does NOT commit anything.
    virtual void SetClipPreview(const std::string& clip_id,
                                double start_seconds,
                                double end_seconds) = 0;
    virtual void ClearClipPreviews() = 0;
    // Rubber-band marquee: [x0, x1] in widget coordinates, drawn across
    // the full track area. `active` false clears it. Zoom uses it for
    // range zoom; Selection uses it for click-drag marquee select.
    virtual void SetMarquee(bool active, float x0, float x1) = 0;

    // --- mutations -> TimelineHooks (the bridge commits via history) ---

    virtual void MoveClip(const std::string& clip_id,
                          double new_start) = 0;
    // new_point is a SOURCE-time trim point — the model's TrimClip
    // contract (TrimEdge::In -> in_point, TrimEdge::Out -> out point;
    // out = in_point + duration * playback_rate).
    virtual void TrimClip(const std::string& clip_id, TrimEdge edge,
                          double new_point) = 0;
    // Ripple trim: forwards to TimelineHooks::on_ripple_trim when
    // wired — the bridge implements trim + gap close/push as ONE
    // history entry. Falls back to on_trim_clip so the gesture still
    // commits a plain trim when the shell has not wired the hook.
    virtual void RippleTrim(const std::string& clip_id, TrimEdge edge,
                            double new_point) = 0;
    virtual void SplitClip(const std::string& clip_id,
                           double at_seconds) = 0;
    // Adjacent pair on one track; delta in TIMELINE seconds — >0 moves
    // the cut right (model RollingEdit semantics).
    virtual void RollingEdit(const std::string& from_id,
                             const std::string& to_id,
                             double delta_seconds) = 0;
    // delta in SOURCE seconds (in_point += delta).
    virtual void SlipClip(const std::string& clip_id,
                          double delta_seconds) = 0;
    // delta in TIMELINE seconds; neighbours absorb the shift.
    virtual void SlideClip(const std::string& clip_id,
                           double delta_seconds) = 0;
    // Premiere Speed/Duration: new_rate > 0, keeps in_point and the
    // consumed source span.
    virtual void SetClipRate(const std::string& clip_id,
                             double new_rate) = 0;

    // --- view controls (surface-local, no hooks) ---

    virtual void PanBy(double seconds) = 0;
    // Multiplies px/second by `factor` keeping `anchor_seconds` fixed
    // under the pointer.
    virtual void ZoomAt(double factor, double anchor_seconds) = 0;
    // Zooms so [t0, t1] fills the visible track area.
    virtual void ZoomToRange(double t0, double t1) = 0;
};

// A stateful pointer-gesture machine. The surface forwards pointer
// events only to the ACTIVE tool; each returns true when it consumed
// the event. Drag state lives inside the tool — SetTool() cancels the
// active gesture first so previews/marquee/status are always unwound.
class TimelineTool {
public:
    virtual ~TimelineTool() = default;

    virtual ToolId Id() const = 0;
    virtual const char* Name() const = 0;
    // Short interaction hint ("arrow", "cut", "roll") — surfaced via
    // the status line; the kit has no OS-cursor switching yet.
    virtual const char* CursorHint() const = 0;

    virtual bool OnPointerDown(const ToolContext& ctx,
                               ToolCommit& commit) {
        (void)ctx;
        (void)commit;
        return false;
    }
    virtual bool OnPointerMove(const ToolContext& ctx,
                               ToolCommit& commit) {
        (void)ctx;
        (void)commit;
        return false;
    }
    virtual bool OnPointerUp(const ToolContext& ctx,
                             ToolCommit& commit) {
        (void)ctx;
        (void)commit;
        return false;
    }
    // Aborts any in-progress gesture: clears previews/marquee and the
    // tool's drag state. Called on tool switch, Escape, or capture
    // loss.
    virtual void Cancel(ToolCommit& commit) {
        dragging_ = false;
        commit.ClearClipPreviews();
        commit.SetMarquee(false, 0.0f, 0.0f);
    }
    bool IsDragging() const { return dragging_; }

protected:
    bool dragging_ = false;
};

// ---- shared helpers (timeline_tools.cc) ----

// True when the clip sits on a locked track — every clip-affecting
// tool refuses the gesture with a "track is locked" status.
bool ClipTrackLocked(const Project& project, const Clip& clip);
// Every clip sharing clip.link_group ("" -> just `clip` itself), in
// project order. Used for linked co-move/co-trim.
std::vector<const Clip*> LinkedClips(const Project& project,
                                     const Clip& clip);
// The earliest clip on clip.track starting at/after `end`
// (adjacency test for rolling edits uses the model's own epsilon).
const Clip* NextClipOnTrack(const Project& project, int track,
                            double after_seconds);
// The latest clip on `track` ending at/before `start`.
const Clip* PrevClipOnTrack(const Project& project, int track,
                            double before_seconds);

// Builds the concrete tool instance for `id` (Selection by default).
std::unique_ptr<TimelineTool> MakeTimelineTool(ToolId id);

}  // namespace XER::VideoEditor::Ui2
