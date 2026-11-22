// Premiere-style timeline editor widget for XER Studio Phase 1D/2.
// Pure XER_UI_Core widget — the Project remains owned by the app
// shell; this view only renders it and asks the shell to commit
// validated TimelineHistory operations.
//
// Phase 2 additions (spec docs/superpowers/specs/
// 2026-11-17-video-editor-phase2.md section 6):
//   * Premiere tool model (V/C/B/N/Y/U/R/H/Z) — state lives in
//     ui/tools/*.h/.cc; the app binds the keys globally and calls
//     SetTool().
//   * marker lane above the ruler (double-click adds, right-click
//     removes), work-area bar with draggable edge handles, track
//     header mute/eye/lock toggles, waveform + poster rendering on
//     clips, linked-clip co-move, locked-track dimming/refusal.
//   * Every clip-affecting gesture funnels through ToolCommit ->
//     Hooks, so multi-clip edits (link groups, ripple trims) stay one
//     undo macro in the shell.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "timeline_model.h"
#include "tools/edit_tool.h"
#include "widgets/widget.h"

namespace video {
class Frame;  // src/video/frame.h — poster hook out-param only
}

namespace XER::VideoEditor {
struct AudioPeaks;  // media_catalog.h — waveform hook out-param only
}

namespace XER::VideoEditor::Ui {

// Tri-state answer of the async media hooks below. A shell serving a
// background-warmed store (the app's media_prep worker pool) returns
// kPending for in-flight decodes — the widget draws the placeholder
// and asks again next frame — and kFailed for permanent misses, which
// the widget negative-caches. A purely synchronous shell simply
// answers kReady/kFailed and behaves exactly like the old bool hook.
enum class MediaLookup {
    kReady,    // out-param filled — usable now
    kPending,  // decode queued/running — retry next frame
    kFailed,   // permanent miss — widget caches the failure
};

class TimelineWidget final : public XER::UI::Widgets::Widget,
                             private ToolCommit {
public:
    struct Hooks {
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
        // Unset -> RippleEditTool falls back to on_trim_clip.
        std::function<void(const std::string&, TrimEdge, double)>
            on_ripple_trim;
        std::function<void(const std::string&)> on_delete_clip;
        // Ripple delete (remove + close the gap on its track): the
        // widget fires it for Alt+Delete on the selected clip.
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
        // Clip junction context menu (right-click on a clip's
        // out-edge): add a transition of `type` to the next clip on
        // the track. duration_seconds <= 0 asks the shell for its
        // default transition duration. script_id is "" unless
        // type == Script.
        std::function<void(const std::string&, Transition::Type,
                           double, const std::string&)>
            on_add_transition;
        // Junction-handle drag: retarget the duration of the
        // transition (from_clip, to_clip) — already clamped to the
        // endpoint clips' available handles by the widget. The shell
        // commits it through TimelineHistory so the gesture is one
        // undoable step.
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
        // worker decodes — never block the UI thread here. get_audio_
        // peaks fills AudioPeaks for the media path; get_poster fills
        // a BGRA video::Frame at the SOURCE seconds given.
        // get_media_bounds feeds the seconds->bucket mapping for
        // waveforms (AudioPeaks carries no sample rate — without this
        // the widget estimates buckets at 48 kHz).
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
        // state so the shell (which also owns the main-toolbar Snap
        // button and the S shortcut) stays the single source of truth
        // — the shell flips its flag and calls SetSnapEnabled back.
        // Unset -> the widget toggles snap_enabled_ itself.
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
        // Clip context menu "Track object…" (and "Track '<class>'…"
        // re-scans narrowed to a class the first scan saw): the shell
        // runs the async scan (ai/object_track.h) and lands
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

    explicit TimelineWidget(Hooks hooks = {});

    void SetProject(const Project* project);
    void SetPlayhead(double seconds);
    // Tells the widget the media pipeline published fresh data (the
    // app's media-prep ConsumeRenderDirty/ConsumeCatalogDirty drains):
    // cached media-duration MISSES are dropped so the next paint
    // re-probes — a waveform that estimated its span before the media
    // landed realigns instead of staying wrong for the session.
    // Positive durations stay cached.
    void NotifyMediaChanged();
    void SetSelectedClip(const std::string& clip_id);
    void SetSnapEnabled(bool enabled);
    bool SnapEnabled() const { return snap_enabled_; }
    void SetZoom(double px_per_second);

    // Phase-2 tool model. The app binds V/C/B/N/Y/U/R/H/Z globally and
    // calls SetTool; switching mid-gesture cancels the active tool
    // first (previews/marquee unwound).
    void SetTool(ToolId tool);
    ToolId ActiveTool() const { return active_tool_; }
    const char* ActiveToolName() const;
    const char* ActiveToolCursorHint() const;

    // Context menu for the point under the cursor — the shell calls this
    // from its tree-level context-menu handler when a right-click falls
    // through. Hit-tests marker lane / ruler / track headers / clips and
    // shows the matching menu. Returns true when a menu was shown.
    bool ShowContextMenu(XER::UI::Layout::Point position);

    // Media-bin drag->drop target API (absolute-space points, same
    // coordinates events/bounds use): TimeAtDropPoint maps a drop x to
    // a timeline time (clamped >= 0); SetExternalDropHover toggles the
    // accent-frame affordance while a bin drag hovers this widget.
    double TimeAtDropPoint(XER::UI::Layout::Point point) const;
    // Track index under the drop point's y (-1 above the lanes, past
    // the last track, or on the ruler/header bands) — lets the shell
    // land bin drops on the lane they were released over.
    int TrackAtDropPoint(XER::UI::Layout::Point point) const;
    void SetExternalDropHover(bool active);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {420.0f, 170.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {1200.0f, 280.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct ClipHit {
        const Clip* clip = nullptr;
        ClipPart part = ClipPart::Body;
    };
    // Track-header affordance hit. Logical button ids:
    //   0 = mute (audio), 1 = hidden/eye (video), 2 = lock,
    //   3 = expand chevron (both kinds), 4 = solo (audio),
    //   5 = record arm (audio).
    // track -1 = no hit.
    struct HeaderHit {
        int track = -1;
        int button = -1;
    };
    enum class WorkDrag { None, In, Out, Move };
    // Junction-overlay edge hit: index into project_->transitions and
    // which window edge is under the pointer (In = left bar, Out =
    // right bar). index < 0 = no hit.
    struct TransitionHandleHit {
        int index = -1;
        TrimEdge edge = TrimEdge::Out;
    };
    // Downsampled whole-media waveform for one pixel budget: 2*columns
    // floats (min,max) covering [0, span_seconds] of source time.
    struct WaveRow {
        double span_seconds = 0.0;
        int columns = 0;
        std::vector<float> minmax;
    };
    struct PosterKey {
        std::string path;
        std::int64_t quantized_ms = 0;
        bool operator==(const PosterKey&) const = default;
    };
    struct PosterKeyHash {
        std::size_t operator()(const PosterKey& key) const {
            return std::hash<std::string>()(key.path) ^
                   (std::hash<std::int64_t>()(key.quantized_ms) << 1);
        }
    };

    double TimeAtX(float x) const;
    float XAtTime(double t) const;
    // Per-track lane height — the header chevron (button id 3)
    // expands a row to a taller lane for waveform/thumbnail room.
    float TrackRowHeight(int index) const;
    int TrackAtY(float y) const;
    XER::UI::Layout::Rect TrackRect(int index) const;
    XER::UI::Layout::Rect HeaderRect(int index) const;
    // Corner-toolbar ghost buttons (magnet/razor/marker/fx), index or
    // -1. Lives in the header-column corner box above the track rows.
    int ToolbarButtonAt(XER::UI::Layout::Point p) const;
    XER::UI::Layout::Rect ToolbarButtonRect(int index) const;
    XER::UI::Layout::Rect ClipRect(const Clip& clip) const;
    // Clip span ignoring the preview overlay (used to draw the dimmed
    // "original position" ghost under a preview).
    XER::UI::Layout::Rect RealClipRect(const Clip& clip) const;
    ClipHit HitClip(XER::UI::Layout::Point p) const;
    HeaderHit HitHeaderButton(XER::UI::Layout::Point p) const;
    // Junction-overlay edge zone under p (~4px each side of the
    // rendered window edges) — the draggable transition-duration
    // handles. Shares the render path's region math incl. the
    // degenerate-zoom minimum span.
    TransitionHandleHit HitTransitionHandle(
        XER::UI::Layout::Point p) const;
    // Handle-clamped duration ceiling for `transition` — media bounds
    // resolved through the get_media_bounds hook for each endpoint
    // (non-media sources count as unbounded).
    double TransitionDragLimit(const Transition& transition) const;
    // The duration a transition renders/drag-targets with right now:
    // the live preview while its handle is being dragged, else the
    // stored duration.
    double EffectiveTransitionDuration(
        const Transition& transition) const;
    const Marker* MarkerAtX(float x) const;
    // Work-area zone under p inside the ruler band: 0 = seek area,
    // 1 = in-edge handle, 2 = out-edge handle, 3 = whole-bar move.
    // (The marker lane is handled separately via MarkerAtX.)
    int TopZoneAt(XER::UI::Layout::Point p) const;
    ToolContext MakeContext(
        const XER::UI::Widgets::Event& event) const;
    EditTool* ActiveToolImpl() const;

    // --- rendering helpers ---
    void RenderMarkers(XER::UI::Rendering::Renderer& renderer,
                       const XER::UI::Theme::Tokens& tokens,
                       const XER::UI::Layout::Rect& bounds);
    void RenderWorkArea(XER::UI::Rendering::Renderer& renderer,
                        const XER::UI::Theme::Tokens& tokens,
                        const XER::UI::Layout::Rect& bounds);
    void RenderHeaderButtons(XER::UI::Rendering::Renderer& renderer,
                             const XER::UI::Theme::Tokens& tokens,
                             const Track& track, int track_index,
                             const XER::UI::Layout::Rect& label_rect);
    // Corner-box toolbar row: magnet snap toggle, razor tool, add
    // marker at playhead, fx summary badge.
    void RenderToolbar(XER::UI::Rendering::Renderer& renderer,
                       const XER::UI::Theme::Tokens& tokens,
                       const XER::UI::Layout::Rect& bounds);
    void RenderWaveform(XER::UI::Rendering::Renderer& renderer,
                        const XER::UI::Theme::Tokens& tokens,
                        const Clip& clip,
                        const XER::UI::Layout::Rect& rect);
    void RenderThumbnails(XER::UI::Rendering::Renderer& renderer,
                          const XER::UI::Theme::Tokens& tokens,
                          const Clip& clip,
                          const XER::UI::Layout::Rect& rect);

    // --- media caches ---
    // Peaks for `path` (nullptr on miss/failure — misses are cached so
    // silent files are not re-probed every frame).
    const AudioPeaks* PeaksFor(const std::string& path);
    double MediaDurationFor(const std::string& path);
    // Whole-media downsampled waveform at `columns` resolution —
    // cached per (path, columns), i.e. per (path, bucket width).
    const WaveRow* WaveFor(const std::string& path, int columns);
    const video::Frame* PosterFor(const std::string& path,
                                  double seconds);
    // Nearest cached poster for `path` by |quantized_ms - t| — drawn
    // while the exact bucket decodes so a thumbnail slot shows a
    // stable (slightly off-time) frame instead of blanking each miss.
    const video::Frame* NearestPoster(const std::string& path,
                                      std::int64_t quantized_ms);

    // --- ToolCommit (private — only tools reach this) ---
    // SetStatus doubles as ToolCommit::SetStatus -> hooks_.on_status.
    void SetStatus(const std::string& text,
                   bool is_error = false) override;
    void SelectClip(const std::string& clip_id) override;
    void RequestRepaint() override { Invalidate(); }
    double SnapCandidate(double seconds, int track) const override;
    void SetClipPreview(const std::string& clip_id,
                        double start_seconds,
                        double end_seconds) override;
    void ClearClipPreviews() override;
    void SetMarquee(bool active, float x0, float x1) override;
    void MoveClip(const std::string& clip_id,
                  double new_start) override;
    void TrimClip(const std::string& clip_id, TrimEdge edge,
                  double new_point) override;
    void RippleTrim(const std::string& clip_id, TrimEdge edge,
                    double new_point) override;
    void SplitClip(const std::string& clip_id,
                   double at_seconds) override;
    void RollingEdit(const std::string& from_id,
                     const std::string& to_id,
                     double delta_seconds) override;
    void SlipClip(const std::string& clip_id,
                  double delta_seconds) override;
    void SlideClip(const std::string& clip_id,
                   double delta_seconds) override;
    void SetClipRate(const std::string& clip_id,
                     double new_rate) override;
    void PanBy(double seconds) override;
    void ZoomAt(double factor, double anchor_seconds) override;
    void ZoomToRange(double t0, double t1) override;

    Hooks hooks_;
    const Project* project_ = nullptr;
    double playhead_ = 0.0;
    std::string selected_clip_id_;
    bool snap_enabled_ = true;
    double px_per_second_ = 72.0;
    double scroll_seconds_ = 0.0;
    // Vertical track scroll in pixels (Alt+wheel) — lets many-track
    // projects scroll under the fixed ruler/marker lanes.
    float track_scroll_ = 0.0f;
    bool ruler_scrub_ = false;   // ruler press-drag emits on_seek
    // Media-bin drag hovering this widget — Render strokes an accent
    // frame so the panel reads as a live drop target.
    bool external_drop_hover_ = false;

    // Premiere-style hover affordances — painted thin borders/glyph
    // tints only, no interaction change.
    std::string hover_clip_id_;
    HeaderHit hover_header_;
    int hover_toolbar_ = -1;
    // Junction-handle hover: the transitions index + edge under the
    // pointer (hot edge bars brighten — the resize-cursor affordance;
    // XER_UI_Core has no OS-cursor switching).
    int hover_transition_ = -1;
    TrimEdge hover_transition_edge_ = TrimEdge::Out;

    // Per-track expand state for the header chevrons (track index ->
    // tall lane). Widget-side only; the model has no row-height field.
    std::unordered_set<int> expanded_tracks_;

    // Tool model — one instance per ToolId, active_tool_ picks the
    // receiver of pointer events.
    ToolId active_tool_ = ToolId::Selection;
    std::map<ToolId, std::unique_ptr<EditTool>> tools_;

    // Preview ghosts (clip id -> [start,end) timeline seconds) and the
    // zoom marquee — set by tools through ToolCommit.
    std::map<std::string, std::pair<double, double>> clip_previews_;
    bool marquee_active_ = false;
    float marquee_x0_ = 0.0f;
    float marquee_x1_ = 0.0f;

    // Work-area bar drag (edges + whole-bar move).
    WorkDrag work_drag_ = WorkDrag::None;
    double work_drag_in_ = 0.0;
    double work_drag_out_ = 0.0;
    double work_drag_anchor_ = 0.0;   // ctx.time at grab (Move)
    double work_drag_orig_in_ = 0.0;  // bar span at grab (Move)
    double work_drag_orig_out_ = 0.0;

    // Junction-handle drag (Selection tool only): resizes the
    // transition's centered duration through its edge bars. The
    // preview duration drives the overlay rect live; the clamped
    // final value commits on mouse-up via on_set_transition_duration.
    bool transition_drag_ = false;
    std::string transition_drag_from_;
    std::string transition_drag_to_;
    TrimEdge transition_drag_edge_ = TrimEdge::Out;   // grabbed side
    double transition_drag_orig_ = 0.0;               // duration at grab
    double transition_drag_preview_ = 0.0;            // live value
    double transition_drag_max_ = 0.0;                // handle ceiling

    // Double-click detection on the ruler/marker lane (adds a marker).
    std::chrono::steady_clock::time_point last_ruler_click_{};
    float last_ruler_click_x_ = -1.0e9f;
    bool last_ruler_click_valid_ = false;

    // Media caches — see PeaksFor/WaveFor/PosterFor. Every container is
    // bounded: *_lru_ deques carry FIFO eviction order for their map/
    // set so a long session can't grow them without bound.
    std::unordered_map<std::string, std::shared_ptr<const AudioPeaks>>
        peaks_cache_;
    std::deque<std::string> peaks_lru_;
    std::unordered_set<std::string> peaks_missing_;
    std::deque<std::string> peaks_missing_lru_;
    std::unordered_map<std::string, WaveRow> wave_cache_;
    std::deque<std::string> wave_lru_;
    std::unordered_map<std::string, double> media_duration_cache_;
    std::deque<std::string> duration_lru_;
    // Poster cache: a real LRU — lookups promote entries, eviction
    // takes the front (previously insertion-order eviction dropped
    // still-visible thumbnails whenever >48 distinct buckets cycled,
    // which read back as a per-frame kPending flicker).
    struct PosterEntry {
        std::shared_ptr<video::Frame> frame;
        std::list<PosterKey>::iterator lru_it;
    };
    std::unordered_map<PosterKey, PosterEntry, PosterKeyHash>
        poster_cache_;
    std::unordered_set<std::string> poster_missing_;  // "path@ms"
    std::deque<std::string> poster_missing_lru_;
    std::list<PosterKey> poster_lru_;   // front = LRU, back = MRU

    XER::UI::Layout::Rect timeline_area_{};
};

}  // namespace XER::VideoEditor::Ui
