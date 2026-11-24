// TimelineBridge — the adapter that binds a TimelineSurface's
// TimelineHooks onto a shell-owned Project + TimelineHistory
// (spec §6: the shell owns document state and commits; surfaces
// only emit hooks).
//
// It mirrors the legacy VideoEditorAppWidget hook wiring
// (ui/video_editor_app.cc ~1685–1840 + the ~3300/4300 command
// methods) — same operation names, same error/status strings, same
// atomic-commit shapes:
//   * every mutation goes through TimelineHistory (never the model
//     directly), so undo labels match the legacy shell's
//     ("Ripple trim", "Move clip", ...);
//   * multi-model calls inside one hook (ripple trim =
//     trim + gap close, linked co-move bursts via the surface's
//     on_gesture_begin/on_gesture_end bracket) land as ONE
//     BeginMacro/EndMacro pair — the hooks contract's documented
//     requirement;
//   * media bounds feed every clip op via a host seam
//     (BoundsForPath equivalent) — unset -> unbounded, the legacy
//     "catalog miss" behavior.
//
// Everything the bridge cannot reach itself (status line, selection
// mirror, playhead push, media caches, clip insertion, repaint) is a
// Host seam the shell fills with plain std::functions — the bridge
// never sees a widget.
#pragma once

#include <functional>
#include <string>

#include "surface_hooks.h"
#include "timeline_history.h"
#include "timeline_model.h"

namespace video {
class Frame;
}

namespace XER::VideoEditor::Ui2 {

class TimelineBridge {
public:
    // Shell-side seams. Everything is optional unless noted — unset
    // seams degrade exactly like the legacy unwired hooks (menu
    // items hide, ops report a status error or run unbounded).
    struct Host {
        // Seek commit: the shell clamps/owns the playhead and pushes
        // it to the surface + preview engine (SetPlayheadSeconds).
        std::function<void(double)> on_seek;
        // Selection mirror: fired whenever the bridge changes the
        // selected clip id — the shell pushes it back to the
        // surface's SetSelectedClip and its panels.
        std::function<void(const std::string&)> on_select;
        // Status line (text, is_error) — the legacy SetStatus.
        std::function<void(const std::string&, bool)> on_status;
        // Fired after every committed mutation — the shell pushes
        // the project/playhead/selection snapshot back into the
        // surface and repaints (the legacy NotifyProjectChanged).
        std::function<void()> on_project_changed;
        // Media seam (legacy BoundsForPath): duration/has-video for
        // a source path, feeding every clip op's bounds check.
        // Unset -> MediaBounds{} (unbounded).
        std::function<MediaBounds(const std::string&)>
            bounds_for_path;
        // Tri-state media caches for the surface's waveform/poster
        // paint — wire these to MediaPrep::LookupPeaks /
        // LookupPoster. Unset -> the hooks bind to a kFailed answer
        // (the surface draws the no-waveform placeholder and
        // negative-caches the miss, never blocking paint).
        std::function<MediaLookup(const std::string&, AudioPeaks*)>
            audio_peaks;
        std::function<MediaLookup(const std::string&, double,
                                  video::Frame*)>
            posters;
        // get_media_bounds for the surface's waveform span +
        // junction-handle ceiling — defaults to bounds_for_path.
        std::function<bool(const std::string&, MediaBounds*)>
            media_bounds;
        // Dropped-clip cleanup (legacy media_prep_.CancelQueued).
        std::function<void(const std::string&)> cancel_queued_media;
        // Snap toggle requests (the surface's corner magnet) — the
        // shell owns the snap flag and calls SetSnapEnabled back.
        std::function<void(bool)> on_snap_toggle;
        // Empty-lane insertion seams (M3+): context-menu
        // "Add … Here" items stay hidden while unset.
        std::function<void(int, SourceKind, double)> add_generated;
        std::function<void(int, double)> add_media;
        // AI / transcript seams — bind only when the host provides
        // them so the context menu stays collapsed otherwise.
        std::function<void(const std::string&)> ai_find_objects;
        std::function<void(const std::string&)> ai_clear_tags;
        std::function<void(const std::string&, double)> smart_reframe;
        std::function<void(const std::string&, const std::string&)>
            track_object;
        std::function<void(const std::string&)> pin_title;
        std::function<void(const std::string&)> pin_mosaic;
        std::function<void(const std::string&)> clear_track;
        // "Auto-blur faces" — YuNet scan -> face ClipTrack ->
        // xer.fx.face_blur effect, all shell-side.
        std::function<void(const std::string&)> face_blur;
        // "Track Mask (AI)…" — the shell arms the monitor click
        // seam, the click becomes the SAM2 prompt, and the async
        // mask scan lands Clip::mask_track + xer.fx.track_mask,
        // all shell-side. "Clear Mask Track" empties it.
        std::function<void(const std::string&)> track_mask;
        std::function<void(const std::string&)> clear_mask;
        // "Smooth slow-mo (AI)" — RIFE bounce of the consumed span
        // -> baked media + clip repoint (ai/rife_interp.h), all
        // shell-side.
        std::function<void(const std::string&)> ai_slowmo;
        std::function<void(const std::string&)> edit_transcript;
    };

    // Borrows both — the shell outlives the bridge. `host` copies;
    // every seam may be rebound later via SetHost.
    TimelineBridge(Project& project, TimelineHistory& history,
                   Host host = {});
    void SetHost(Host host);

    // The hook table the surface binds via SetHooks — built once in
    // the ctor; stable for the bridge's lifetime.
    const TimelineHooks& Hooks() const { return hooks_; }

    const std::string& SelectedClipId() const {
        return selected_clip_id_;
    }
    double PlayheadSeconds() const { return playhead_; }
    // Transition defaults re-armed by every successful add (the
    // legacy transition_type_/duration_/script_ triplet).
    void SetTransitionDefaults(Transition::Type type, double duration,
                               std::string script);
    double DefaultTransitionDuration() const {
        return transition_duration_;
    }

    // ---- hook targets (legacy command names kept greppable) ----
    void SelectClip(const std::string& clip_id);
    void Seek(double seconds);
    void MoveClip(const std::string& clip_id, double start);
    void TrimClip(const std::string& clip_id, TrimEdge edge,
                  double point);
    void RippleTrimClip(const std::string& clip_id, TrimEdge edge,
                        double point);
    void DeleteClip(const std::string& clip_id);
    void RippleDeleteClipById(const std::string& clip_id);
    void SplitClip(const std::string& clip_id, double at_seconds);
    void RollingEditClips(const std::string& from_id,
                          const std::string& to_id, double delta);
    void SlipClipBy(const std::string& clip_id, double delta_src);
    void SlideClipBy(const std::string& clip_id, double delta_tl);
    void SetClipRateBy(const std::string& clip_id, double rate);
    // Adds a transition from `clip_id` to the next clip on its track
    // (duration <= 0 uses the re-armed default). `script` is the
    // type-scoped variant string: JS script id for Type::Script, the
    // avfilter token ("radial", ...) for Type::Xfade, the "xer.tr.*"
    // transition id for Type::XerFx, "" otherwise.
    void AddTransitionAfter(const std::string& clip_id,
                            Transition::Type type, double duration,
                            const std::string& script);
    void AddTransitionPair(const std::string& from_id,
                           const std::string& to_id,
                           Transition::Type type, double duration,
                           const std::string& script);
    void SetTransitionDurationBy(const std::string& from_id,
                                 const std::string& to_id,
                                 double duration);
    void AddMarkerAt(double t, const std::string& name);
    void RemoveMarkerAt(double t);
    void SetWorkAreaRange(double in_s, double out_s);
    void SetTrackFlag(int track, char which, bool on);

private:
    MediaBounds BoundsForPath(const std::string& path) const;
    void NotifyProjectChanged();
    void SetStatus(const std::string& text, bool is_error = false);
    void BuildHooks();

    Project& project_;
    TimelineHistory& history_;
    Host host_;
    TimelineHooks hooks_;
    std::string selected_clip_id_;
    double playhead_ = 0.0;
    // Re-armed transition defaults (legacy transition_type_/
    // duration_/script_).
    Transition::Type transition_type_ = Transition::Type::Crossfade;
    double transition_duration_ = 0.5;
    std::string transition_script_;
};

}  // namespace XER::VideoEditor::Ui2
