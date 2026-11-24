// Surface hook contracts — ported from ui/timeline_widget.h and
// ui/monitor_widget.h (verified behavior, spec §6/§9). These are the
// data shapes the M2+ surfaces bind to; the shell implements them
// against TimelineHistory / PreviewEngine / the media-prep pool.
//
// Rules every surface follows (spec §6 paint contract):
//   1. Paint only inside its content rect; snap 1px lines via
//      Layout::Snap.
//   2. No document mutation in Render/HandleEvent — emit hooks; all
//      edits funnel through TimelineHistory/PhotoHistory (one gesture =
//      one undo macro, mirroring the old ToolCommit pattern).
//   3. Surfaces own *view state* (zoom, scroll, tool selection); the
//      shell owns *document state* and pushes snapshots (SetProject
//      model — old monitor_widget.h precedent).
//   4. Tri-state async media hooks (MediaLookup) — NEVER block paint
//      on decode.
//   5. Playhead/meter repaint = Invalidate(rect) only.
//   6. Hit zones computed in Layout-time DIP space; surfaces handle
//      their own double-click synthesis and hover cursors.
#pragma once

#include <functional>
#include <string>

#include "timeline_model.h"

namespace video {
class Frame;  // src/video/frame.h — poster hook out-param only
}

namespace XER::VideoEditor {
struct AudioPeaks;  // media_catalog.h — waveform hook out-param only
}

namespace XER::VideoEditor::Ui2 {

// Tri-state answer of the async media hooks below. A shell serving a
// background-warmed store (media_prep worker pool) returns kPending
// for in-flight decodes — the surface draws the placeholder and asks
// again next frame — and kFailed for permanent misses, which the
// surface negative-caches. A purely synchronous shell answers
// kReady/kFailed (the old bool-hook behavior).
enum class MediaLookup {
    kReady,    // out-param filled — usable now
    kPending,  // decode queued/running — retry next frame
    kFailed,   // permanent miss — surface caches the failure
};

// TimelineSurface (S1) hook contract — TimelineWidget::Hooks ported
// verbatim. The shell binds every member; surfaces may fire them from
// any gesture handler.
struct TimelineHooks {
    std::function<void(const std::string&)> on_select_clip;
    std::function<void(double)> on_seek;
    // Move/trim also fire once per additional member of the dragged
    // clip's link_group (same delta) — the shell should wrap the
    // burst of same-gesture commits in ONE history macro.
    std::function<void(const std::string&, double)> on_move_clip;
    // new_point is a SOURCE-time point (model TrimClip contract).
    std::function<void(const std::string&, TrimEdge, double)>
        on_trim_clip;
    // Ripple trim (B tool): trim + close the freed gap / push
    // followers on the clip's track — the shell should implement
    // it atomically (TrimClip + gap close in one history entry).
    // Unset -> the ripple tool falls back to on_trim_clip.
    std::function<void(const std::string&, TrimEdge, double)>
        on_ripple_trim;
    std::function<void(const std::string&)> on_delete_clip;
    // Ripple delete (remove + close the gap on its track): the
    // surface fires it for Alt+Delete on the selected clip.
    std::function<void(const std::string&)> on_ripple_delete;
    std::function<void(const std::string&, double)> on_split_clip;
    // Rolling edit (N tool): (from_id, to_id, delta TIMELINE s).
    std::function<void(const std::string&, const std::string&,
                       double)> on_rolling_edit;
    // Slip (Y): delta SOURCE s. Slide (U): delta TIMELINE s.
    std::function<void(const std::string&, double)> on_slip_clip;
    std::function<void(const std::string&, double)> on_slide_clip;
    // Rate stretch (R): new playback_rate (Speed/Duration).
    std::function<void(const std::string&, double)> on_rate_stretch;
    // Clip junction context menu: add a transition of `type` to the
    // next clip on the track. duration_seconds <= 0 asks the shell
    // for its default transition duration. script_id is "" unless
    // type == Script.
    std::function<void(const std::string&, Transition::Type,
                       double, const std::string&)>
        on_add_transition;
    // Junction-handle drag: retarget the transition duration
    // (from_clip, to_clip) — already clamped to the endpoint clips'
    // available handles by the surface. The shell commits it through
    // TimelineHistory so the gesture is one undoable step.
    std::function<void(const std::string&, const std::string&,
                       double)> on_set_transition_duration;
    // Marker lane: (time_seconds, name — "" lets the shell pick a
    // default label) / remove the marker at `time`.
    std::function<void(double, const std::string&)> on_add_marker;
    std::function<void(double)> on_remove_marker;
    // Work-area bar edges (in_seconds, out_seconds; out <= in
    // resets to the whole project — model SetWorkArea contract).
    std::function<void(double, double)> on_set_work_area;
    // Track header toggles: (track_index, enabled).
    std::function<void(int, bool)> on_track_mute;
    std::function<void(int, bool)> on_track_hidden;
    std::function<void(int, bool)> on_track_locked;
    // Optional media queries — every one degrades gracefully when
    // unset. Tri-state non-blocking contract (see MediaLookup):
    // the shell serves a warm cache and answers kPending while a
    // worker decodes — never block the UI thread here.
    // get_audio_peaks fills AudioPeaks for the media path; get_poster
    // fills a BGRA video::Frame at the SOURCE seconds given.
    // get_media_bounds feeds the seconds->bucket mapping for
    // waveforms (AudioPeaks carries no sample rate — without this
    // the surface estimates buckets at 48 kHz).
    std::function<MediaLookup(const std::string&, AudioPeaks*)>
        get_audio_peaks;
    std::function<MediaLookup(const std::string&, double,
                              video::Frame*)>
        get_poster;
    std::function<bool(const std::string&, MediaBounds*)>
        get_media_bounds;
    std::function<void(const std::string&, bool)> on_status;
    // Empty-lane context menu "Add … Here" items: create a
    // generated clip of `kind` (matte/bars/text) on `track` at
    // `at_seconds` timeline time. The shell owns any config
    // dialog (matte color pick) and the clip creation itself.
    std::function<void(int, SourceKind, double)> on_add_generated;
    // Empty-lane context menu: pick a media file and place it on
    // `track` at `at_seconds` (kind-mismatched/locked targets
    // fall back to the shell's normal placement rules).
    std::function<void(int, double)> on_add_media;
    // Corner-toolbar magnet button: fires with the REQUESTED snap
    // state so the shell (which also owns the toolbar Snap button
    // and the S shortcut) stays the single source of truth — the
    // shell flips its flag and calls SetSnapEnabled back.
    // Unset -> the surface toggles its own snap flag.
    std::function<void(bool)> on_snap_toggle;
    // Clip context menu "AI: Find Objects" — the shell runs the
    // async scan (ai_tags.h) and lands ClipTag ranges on the clip.
    // "AI: Clear Tags" empties Clip::tags again.
    std::function<void(const std::string&)> on_ai_find_objects;
    std::function<void(const std::string&)> on_ai_clear_tags;
    // Clip context menu "Smart Reframe → <ratio>": the shell runs
    // the async subject scan (ai/smart_reframe.h), stores the
    // ClipReframe track on the clip and applies the
    // xer.fx.smart_reframe effect at the chosen target aspect
    // (w/h: 9:16 = 0.5625, 1:1 = 1.0, 4:5 = 0.8).
    std::function<void(const std::string&, double)>
        on_smart_reframe;
    // Clip context menu "Track object…" / "Track '<class>'…": the
    // shell runs the async scan (ai/object_track.h) and lands
    // Clip::object_track. Second arg is the class filter — "" for
    // the primary-subject policy. "Pin Title"/"Pin Mosaic" follow
    // the stored track; "Clear" empties it.
    std::function<void(const std::string&,
                       const std::string& label_filter)>
        on_track_object;
    std::function<void(const std::string&)> on_pin_title;
    std::function<void(const std::string&)> on_pin_mosaic;
    std::function<void(const std::string&)> on_clear_track;
    // Clip context menu "Auto-blur faces": the shell runs the async
    // YuNet scan (ai/face_detect.h ScanClipFaces), stores the
    // ClipTrack labeled "face" on Clip::object_track and applies the
    // builtin:xer.fx.face_blur effect — the auto-censor preset.
    std::function<void(const std::string&)> on_face_blur;
    // Clip context menu "Track Mask (AI)…" / "Clear Mask Track":
    // the shell arms the monitor click-point seam, the next frame
    // click becomes the SAM2 prompt, and an async job
    // (ai/sam2_track.h ScanClipMaskTrack) lands Clip::mask_track +
    // the builtin:xer.fx.track_mask effect. Clear empties the mask
    // track (the effect stays — it no-ops on an empty track).
    std::function<void(const std::string&)> on_track_mask;
    std::function<void(const std::string&)> on_clear_mask;
    // Clip context menu "Smooth slow-mo (AI)": the shell runs the
    // async RIFE bounce (ai/rife_interp.h + ai/ai_bounce.h) over the
    // clip's consumed source span, writes
    // <stem>.xer.slowmo<N>x.mp4 next to the media, and repoints the
    // clip at the baked file (rate becomes rate*N, == 1 for a clean 1/N slow) through
    // history. The surface enables it for slowed media clips only
    // (playback_rate < 1).
    std::function<void(const std::string&)> on_ai_slowmo;
    // Clip context menu "Edit by Transcript": the shell selects
    // the clip, focuses the Transcript dock and (cache miss)
    // starts the whisper job — media clips only.
    std::function<void(const std::string&)> on_edit_transcript;
    // Optional gesture bracketing (ui2 addition fulfilling the
    // documented "wrap the burst of same-gesture commits in ONE
    // history macro" requirement above): the surface fires
    // on_gesture_begin(label) before the first edit hook of a
    // pointer/key gesture and on_gesture_end() after the last —
    // every hook call in between (linked co-move bursts, ripple
    // trims, multi-member trims) belongs to that gesture. The
    // bridge maps the pair onto TimelineHistory::BeginMacro/
    // EndMacro. Unset -> every hook call commits individually
    // (legacy behavior).
    std::function<void(const char*)> on_gesture_begin;
    std::function<void()> on_gesture_end;
};

// MonitorSurface transform-gizmo target — the payload of
// MonitorHooks::gizmo_target (ui2 addition: Premiere "Motion" /
// After-Effects direct manipulation on the program monitor).
// `transform` is the clip's EFFECTIVE transform at the program
// playhead (keyframe-resolved — the box tracks what the user sees);
// src_* are the clip's decoded source px, canvas_* the sequence
// dims. Empty clip_id hides the box (no selection, clip inactive at
// the playhead, audio/hidden/locked track, or degenerate dims).
struct MonitorGizmoTarget {
    std::string clip_id;
    Transform transform;
    int src_w = 0, src_h = 0;
    int canvas_w = 0, canvas_h = 0;
};

// MonitorSurface (S2) hook contract — the frame is drawn by the
// surface; chrome (transport row, timecode, quality select, badges)
// stays widget-composed per spec §5.
struct MonitorHooks {
    // Fired on any press inside the monitor — the shell uses it to
    // route keyboard transport (Space/JKL/arrows/I/O) at this monitor
    // instead of the other one (last-active-viewer rule →
    // ShortcutRouter::ActivateScope).
    std::function<void()> on_activate;
    // Fitted-frame scrub → program/source seconds.
    std::function<void(double)> on_time_changed;
    // Click inside the fitted frame in frame-normalized coordinates
    // (mask/AI click points).
    std::function<void(float, float)> on_click_point;
    std::function<void()> on_toggle_play;
    std::function<void(const std::string&, bool)> on_status;

    // ---- transform gizmo (program scope; ui2 addition) -------------
    // Direct manipulation on the program monitor — the selected
    // clip's transformed rect drawn with 8 scale handles, a rotation
    // stem and a center anchor. All optional: unset members degrade
    // to "no gizmo" / "that sub-gesture inert", never a mutation.
    //
    // gizmo_target — polled per paint and at gesture start; the
    //   shell resolves the timeline selection into a
    //   MonitorGizmoTarget ("" clip_id hides the box).
    // gizmo_pick — press-to-select hit test: maps a frame-normalized
    //   point to the topmost clip id whose warped rect contains it
    //   at the playhead ("" = none). Armed click points still win —
    //   the surface skips the whole gizmo path while armed.
    // on_gizmo_select — fired with the picked id (the shell mirrors
    //   it through the same selection seam the timeline uses).
    // on_gizmo_begin/end — gesture bracketing, the
    //   TimelineHooks::on_gesture_begin/end discipline: the shell
    //   maps the pair onto ONE history BeginMacro/EndMacro.
    // on_gizmo_preview — live transform during a drag, IN-MEMORY
    //   paint state only (never history). The final frame of a
    //   cancelled gesture (Esc) re-fires the original transform so
    //   the shell's preview write restores.
    // on_gizmo_commit — fired ONCE on release with the final
    //   transform; the shell lands it through
    //   TimelineHistory::ReplaceClip (the inspector's
    //   on_commit_clips path) inside the gesture macro — one undo
    //   step per drag.
    // on_gizmo_reset — double-click inside the box; the shell
    //   substitutes the clip's imported placement
    //   (FitTransformToCanvas) and commits through the same path.
    // gizmo_snap_enabled — read live per gesture; the shell binds
    //   the timeline's magnet flag so one toggle rules both.
    std::function<MonitorGizmoTarget()> gizmo_target;
    std::function<std::string(float, float)> gizmo_pick;
    std::function<void(const std::string&)> on_gizmo_select;
    std::function<void(const char*)> on_gizmo_begin;
    std::function<void()> on_gizmo_end;
    std::function<void(const std::string&, const Transform&)>
        on_gizmo_preview;
    std::function<void(const std::string&, const Transform&)>
        on_gizmo_commit;
    std::function<void(const std::string&)> on_gizmo_reset;
    std::function<bool()> gizmo_snap_enabled;
};

}  // namespace XER::VideoEditor::Ui2
