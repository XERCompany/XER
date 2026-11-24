// TimelineSurface — the S1 timeline surface of the ui2 rebuild
// (spec §6). Pure widget-kit Widget: the Project stays owned by the
// shell (pushed via SetProject), every document mutation leaves
// through the Ui2::TimelineHooks the shell binds, and all view state
// (zoom, scroll, tool, per-track expand) is surface-local.
//
// Ported contract (mirrors ui/timeline_widget.{h,cc} — the legacy
// implementation stays untouched):
//   * Premiere tool model V/C/B/N/Y/U/R/H/Z via ui2/timeline_tools
//     (same ToolContext snapshot, same ToolCommit -> hooks funnel, so
//     linked-group gestures reach the bridge as one hook burst and
//     land as ONE history macro).
//   * marker lane (double-click adds, right-click removes via the
//     context menu), work-area bar with draggable edges, track
//     headers with mute/eye/lock/solo/arm chips and an expand
//     chevron, clip cards with type accents + fx/link chips +
//     waveform/poster content, junction transitions with
//     centered-duration drag handles, zoom marquee, playhead with
//     column-scoped invalidation, external-drop hover frame.
//   * media queries are tri-state (MediaLookup kReady/kPending/
//     kFailed) and NEVER block paint — pending decodes draw
//     placeholders and are re-asked next frame; failures are
//     negative-cached (bounded).
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

#include "surface_hooks.h"
#include "timeline_model.h"
#include "timeline_tools.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {

class TimelineSurface final : public XER::UI::Widgets::Widget,
                              private ToolCommit {
public:
    explicit TimelineSurface(TimelineHooks hooks = {});
    // Rebinds the hook table — the bridge installs its translation
    // layer after the shell constructs the surface.
    void SetHooks(TimelineHooks hooks);
    const TimelineHooks& Hooks() const { return hooks_; }

    void SetProject(const Project* project);
    const Project* GetProject() const { return project_; }
    // Playhead setter for the host (preview engine / seek commit):
    // scrolls the view to follow when the head leaves the window and
    // invalidates ONLY the old+new playhead columns otherwise
    // (spec §6 rule 5 — playhead ticks must not repaint the surface).
    void SetPlayheadSeconds(double seconds);
    double PlayheadSeconds() const { return playhead_; }
    void SetSelectedClip(const std::string& clip_id);
    const std::string& SelectedClipId() const {
        return selected_clip_id_;
    }
    // Render-status strip (Premiere's green/yellow/red needs-render
    // bar): the ClipCostClass ordinal the strip draws for clip_id,
    // or -1 when the hook is unwired — smoke/debug seam.
    int CostClassOf(const std::string& clip_id) const;
    void SetSnapEnabled(bool enabled);
    bool SnapEnabled() const { return snap_enabled_; }
    // Snap-indicator seam: while a drag resolves through
    // SnapCandidate to a snapped time, this reports that time (the
    // Render pass draws a full-height guide line there); -1 hides.
    // Cleared on the release that ends the drag.
    double SnapIndicatorSeconds() const { return snap_indicator_t_; }
    // The snap query itself — read-only, so it stays public (the
    // mutating ToolCommit ops below remain tool-private); the
    // snap-indicator smoke resolves candidates through it.
    double SnapCandidate(double seconds, int track) const override;
    void SetZoom(double px_per_second);
    double PxPerSecond() const { return px_per_second_; }
    void SetScrollSeconds(double seconds);
    double ScrollSeconds() const { return scroll_seconds_; }
    // Strip's track-height control: scales the compact lane height
    // (expanded chevron rows stay proportionally taller).
    void SetTrackHeight(float height_px);
    float TrackHeight() const { return track_height_; }
    // Strip's linkage toggle: when off, Selection ignores link_group
    // membership (Premiere's "Linked Selection" off state).
    void SetLinkEditsEnabled(bool enabled);
    bool LinkEditsEnabled() const { return link_edits_enabled_; }
    // Zoom so the whole project fills the track area (strip "fit").
    void ZoomToFit();
    // The shell calls this after a media-lookup result lands so
    // failed/zero probes re-ask and the surface repaints — the
    // async-seam counterpart of MediaPrep::ConsumeRenderDirty.
    void NotifyMediaChanged();
    // Touches the async media seams (peaks + poster + duration) for
    // every clip on the timeline — a legitimate cache-warmer and the
    // headless smoke's proof that lookups fire without blocking.
    // Returns the number of distinct source paths probed.
    std::size_t PrimeMediaLookups();

    // Premiere-style tool model. The shell binds V/C/B/N/Y/U/R/H/Z
    // through ShortcutRouter and calls SetTool; switching
    // mid-gesture cancels the active tool first (previews/marquee
    // unwound). SetToolIndex takes the strip button order.
    void SetTool(ToolId tool);
    void SetToolIndex(int index) { SetTool(ToolIdForIndex(index)); }
    ToolId ActiveTool() const { return active_tool_; }
    const char* ActiveToolName() const;
    const char* ActiveToolCursorHint() const;
    // Fired whenever the active tool id changes (strip check-state
    // sync — also covers SetTool calls from inside the surface,
    // e.g. the corner razor button).
    void SetOnToolChanged(std::function<void(ToolId)> callback);

    // Context menu for the point under the cursor — the shell calls
    // this from its tree-level context-menu handler when a
    // right-click falls through. Hit-tests marker lane / ruler /
    // track headers / clips and shows the matching menu. Returns
    // true when a menu was shown.
    bool ShowContextMenu(XER::UI::Layout::Point position);

    // Media-bin drag->drop target API (absolute-space points, same
    // coordinates events/bounds use): TimeAtDropPoint maps a drop x
    // to a timeline time (clamped >= 0); SetExternalDropHover
    // toggles the accent-frame affordance while a bin drag hovers
    // this surface.
    double TimeAtDropPoint(XER::UI::Layout::Point point) const;
    // Track index under the drop point's y (-1 above the lanes, past
    // the last track, or on the ruler/header bands).
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

    // ---- layout-time geometry (DIP space; also the smoke's
    // geometry assertions — time<->px, track rows, junction zones) --
    double TimeAtX(float x) const;
    float XAtTime(double t) const;
    // Per-track lane height — the header chevron (button id 3)
    // expands a row to a taller lane for waveform/thumbnail room.
    float TrackRowHeight(int index) const;
    int TrackAtY(float y) const;
    XER::UI::Layout::Rect TrackRect(int index) const;
    XER::UI::Layout::Rect HeaderRect(int index) const;
    // Clip span honoring a live tool preview (the ghost position).
    XER::UI::Layout::Rect ClipRect(const Clip& clip) const;
    // Clip span ignoring the preview overlay.
    XER::UI::Layout::Rect RealClipRect(const Clip& clip) const;
    // Junction-overlay edge zone under p (~4px each side of the
    // rendered window edges) — the draggable transition-duration
    // handles. index < 0 = no hit.
    struct TransitionHandleHit {
        int index = -1;
        TrimEdge edge = TrimEdge::Out;
    };
    TransitionHandleHit HitTransitionHandle(
        XER::UI::Layout::Point p) const;
    // Clip body/edge under p — public so hosts and the smoke can
    // reuse the surface's DIP-space hit zones.
    struct ClipHit {
        const Clip* clip = nullptr;
        ClipPart part = ClipPart::Body;
    };
    ClipHit HitClip(XER::UI::Layout::Point p) const;

private:
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
    // Downsampled whole-media waveform for one pixel budget:
    // 2*columns floats (min,max) covering [0, span_seconds] of
    // source time.
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

    // Corner-toolbar ghost buttons (magnet/razor/marker/fx), index or
    // -1. Lives in the header-column corner box above the track rows.
    int ToolbarButtonAt(XER::UI::Layout::Point p) const;
    XER::UI::Layout::Rect ToolbarButtonRect(int index) const;
    HeaderHit HitHeaderButton(XER::UI::Layout::Point p) const;
    // Handle-clamped duration ceiling for `transition` — media
    // bounds resolved through the get_media_bounds hook for each
    // endpoint (non-media sources count as unbounded).
    double TransitionDragLimit(const Transition& transition) const;
    // The duration a transition renders/drag-targets with right
    // now: the live preview while its handle is being dragged, else
    // the stored duration.
    double EffectiveTransitionDuration(
        const Transition& transition) const;
    const Marker* MarkerAtX(float x) const;
    // Work-area zone under p inside the ruler band: 0 = seek area,
    // 1 = in-edge handle, 2 = out-edge handle, 3 = whole-bar move.
    int TopZoneAt(XER::UI::Layout::Point p) const;
    ToolContext MakeContext(
        const XER::UI::Widgets::Event& event) const;
    TimelineTool* ActiveToolImpl() const;

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
    // Peaks for `path` (nullptr on miss/failure — misses are cached
    // so silent files are not re-probed every frame).
    const AudioPeaks* PeaksFor(const std::string& path);
    double MediaDurationFor(const std::string& path);
    // Whole-media downsampled waveform at `columns` resolution —
    // cached per (path, columns), i.e. per (path, bucket width).
    const WaveRow* WaveFor(const std::string& path, int columns);
    const video::Frame* PosterFor(const std::string& path,
                                  double seconds);
    // Nearest cached poster for `path` by |quantized_ms - t| —
    // drawn while the exact bucket decodes so a thumbnail slot
    // shows a stable (slightly off-time) frame instead of blanking.
    const video::Frame* NearestPoster(const std::string& path,
                                      std::int64_t quantized_ms);

    // RAII bracket around a burst of edit hooks — fires the optional
    // TimelineHooks::on_gesture_begin/on_gesture_end pair. The bridge
    // maps the pair onto TimelineHistory::BeginMacro/EndMacro so one
    // pointer gesture is one undo step; unset hooks -> no-op.
    struct GestureScope {
        GestureScope(TimelineSurface& surface, const char* label);
        ~GestureScope();
        GestureScope(const GestureScope&) = delete;
        GestureScope& operator=(const GestureScope&) = delete;
        TimelineSurface* surface_ = nullptr;
        bool active_ = false;
    };

    // --- ToolCommit (private — only tools reach this) ---
    void SetStatus(const std::string& text,
                   bool is_error = false) override;
    void SelectClip(const std::string& clip_id) override;
    void RequestRepaint() override { Invalidate(); }
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

    TimelineHooks hooks_;
    const Project* project_ = nullptr;
    double playhead_ = 0.0;
    std::string selected_clip_id_;
    bool snap_enabled_ = true;
    double px_per_second_ = 72.0;
    double scroll_seconds_ = 0.0;
    // Vertical track scroll in pixels (Alt+wheel) — lets many-track
    // projects scroll under the fixed ruler/marker lanes.
    float track_scroll_ = 0.0f;
    // Strip's track-height control scales the compact lane.
    float track_height_ = 64.0f;
    // Strip's "Linked Selection" toggle (Premiere parity).
    bool link_edits_enabled_ = true;
    bool ruler_scrub_ = false;   // ruler press-drag emits on_seek
    // Media-bin drag hovering this surface — Render strokes an
    // accent frame so the panel reads as a live drop target.
    bool external_drop_hover_ = false;

    // Premiere-style hover affordances — painted thin borders/glyph
    // tints only, no interaction change.
    std::string hover_clip_id_;
    HeaderHit hover_header_;
    int hover_toolbar_ = -1;
    // Junction-handle hover: the transitions index + edge under the
    // pointer (hot edge bars brighten — the resize-cursor
    // affordance; the kit has no OS-cursor switching).
    int hover_transition_ = -1;
    TrimEdge hover_transition_edge_ = TrimEdge::Out;

    // Per-track expand state for the header chevrons (track index ->
    // tall lane). Surface-side only; the model has no row-height
    // field.
    std::unordered_set<int> expanded_tracks_;

    // Tool model — one instance per ToolId, active_tool_ picks the
    // receiver of pointer events.
    ToolId active_tool_ = ToolId::Selection;
    std::map<ToolId, std::unique_ptr<TimelineTool>> tools_;
    std::function<void(ToolId)> on_tool_changed_;

    // Preview ghosts (clip id -> [start,end) timeline seconds) and
    // the zoom marquee — set by tools through ToolCommit.
    std::map<std::string, std::pair<double, double>> clip_previews_;
    bool marquee_active_ = false;
    float marquee_x0_ = 0.0f;
    float marquee_x1_ = 0.0f;

    // Snap indicator — the last snapped drag time (mutable:
    // SnapCandidate is a const tool-facing seam, so the record lives
    // here; -1 = hidden). MouseUp clears; Render draws the guide.
    mutable double snap_indicator_t_ = -1.0;

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
    double transition_drag_orig_ = 0.0;               // at grab
    double transition_drag_preview_ = 0.0;            // live value
    double transition_drag_max_ = 0.0;                // ceiling

    // Double-click detection on the ruler/marker lane (adds a marker).
    std::chrono::steady_clock::time_point last_ruler_click_{};
    float last_ruler_click_x_ = -1.0e9f;
    bool last_ruler_click_valid_ = false;

    // Media caches — see PeaksFor/WaveFor/PosterFor. Every container
    // is bounded: *_lru_ deques carry FIFO eviction order for their
    // map/set so a long session can't grow them without bound.
    std::unordered_map<std::string,
                       std::shared_ptr<const AudioPeaks>>
        peaks_cache_;
    std::deque<std::string> peaks_lru_;
    std::unordered_set<std::string> peaks_missing_;
    std::deque<std::string> peaks_missing_lru_;
    std::unordered_map<std::string, WaveRow> wave_cache_;
    std::deque<std::string> wave_lru_;
    std::unordered_map<std::string, double> media_duration_cache_;
    std::deque<std::string> duration_lru_;
    // Poster cache: a real LRU — lookups promote entries, eviction
    // takes the front.
    struct PosterEntry {
        std::shared_ptr<video::Frame> frame;
        std::list<PosterKey>::iterator lru_it;
    };
    std::unordered_map<PosterKey, PosterEntry, PosterKeyHash>
        poster_cache_;
    std::unordered_set<std::string> poster_missing_;  // "path@ms"
    std::deque<std::string> poster_missing_lru_;
    std::list<PosterKey> poster_lru_;   // front = LRU, back = MRU

    // Render-status strip state — per-clip ClipCostClass ordinals
    // pulled through TimelineHooks::get_clip_cost_class. Refreshed on
    // SetProject and on a ~500 ms cadence in Tick: the AutoCacher
    // keeps landing coverage without any document mutation, so
    // SetProject alone can't drive it.
    void RefreshCostClasses();
    std::unordered_map<std::string, int> clip_cost_class_;
    std::chrono::steady_clock::time_point cost_refresh_{};

    XER::UI::Layout::Rect timeline_area_{};
};

}  // namespace XER::VideoEditor::Ui2
