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
    // Clip context menu "Edit by Transcript": the shell selects
    // the clip, focuses the Transcript dock and (cache miss)
    // starts the whisper job — media clips only.
    std::function<void(const std::string&)> on_edit_transcript;
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
};

}  // namespace XER::VideoEditor::Ui2
