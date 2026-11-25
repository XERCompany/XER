// MonitorSurface (spec §6 S2) — the canvas-drawn core of a ui2
// monitor: letterboxed BGRA frame + all the overlays the old
// ui/monitor_widget.cc painted (safe margins 90%/80% + center cross,
// rule-of-thirds grid, mini ruler with work-area brackets + loop band
// + marker flags + playhead tick, AI click-point markers, SmartSelect
// mask tint + traced contour) plus monitor view state (zoom/pan).
//
// Composition: the widget owns ONE kit `Widgets::Surface` child that
// fills its bounds and carries the paint/event callbacks — all chrome
// (transport row, timecode, quality select, badges, meters) stays
// widget-composed in monitor_panel.h per spec §5.
//
// Paint-contract notes (spec §6):
//   * the surface owns VIEW state (zoom/pan, overlay toggles); the
//     shell owns DOCUMENT state and pushes it (SetDuration/SetTime/
//     SetWorkArea/SetMarkers/SetFrame — old monitor_widget.h
//     SetProject precedent, flattened to scalars).
//   * no document mutation in Render/HandleEvent — gestures emit
//     MonitorHooks (surface_hooks.h) and the shell decides.
//   * frame/meter/playhead updates call Invalidate(region) only —
//     the video area for frames, the ruler strip for the playhead.
//
// Frame feed (two equivalent paths — the shell uses whichever fits):
//   * SetFrame(bgra,w,h)   — push: VeApp marshals PreviewEngine sink
//                            frames through it (old SetExternalFrame).
//   * SetFrameProvider(fn) — pull: the paint callback asks each frame;
//                            used by the smoke harness and by future
//                            pull-driven feeds (scopes).
//
// Click semantics (old-monitor parity):
//   * press anywhere -> on_activate (last-active-viewer rule).
//   * click/drag on the ruler strip -> on_time_changed(seconds).
//   * click inside the fitted frame: while click points are armed
//     (SetClickPointsArmed — the lab's SmartSelect seam) fires
//     on_click_point(nx,ny) in FRAME-normalized [0,1] coordinates;
//     otherwise fires on_toggle_play.
//   * wheel over the frame zooms at the cursor; dragging a zoomed
//     frame pans; double-click resets to fit.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "layout/geometry.h"
#include "render/video_frame.h"  // render::VideoFrame (zero-copy feed)
#include "surface_hooks.h"
#include "timeline_model.h"   // Marker
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Surface;
}
namespace XER::VideoEditor::Lab {
struct IntPoint;
}

namespace XER::VideoEditor::Ui2 {

class MonitorSurface final : public XER::UI::Widgets::Widget {
public:
    enum class Id { Program, Source };

    // One BGRA8 frame for the pull path — shared so a provider can
    // hand over decoded buffers without a copy.
    struct Frame {
        std::shared_ptr<const std::vector<std::uint8_t>> bgra;
        int width = 0;
        int height = 0;
    };
    using FrameProvider = std::function<Frame()>;

    static std::shared_ptr<MonitorSurface> Create(
        Id id = Id::Program);

    ~MonitorSurface() override;

    Id GetId() const { return id_; }

    // ---- hooks (MonitorHooks contract — surface_hooks.h) ----
    void SetHooks(MonitorHooks hooks);
    const MonitorHooks& Hooks() const { return hooks_; }
    // Mutable access for seam-by-seam wiring (the panel binds the
    // transport members via the SetOn* setters at construction; the
    // shell binds the gizmo members on top).
    MonitorHooks& MutableHooks() { return hooks_; }
    void SetOnActivate(std::function<void()> cb);
    void SetOnTimeChanged(std::function<void(double)> cb);
    void SetOnClickPoint(std::function<void(float, float)> cb);
    void SetOnTogglePlay(std::function<void()> cb);
    void SetOnStatus(
        std::function<void(const std::string&, bool)> cb);
    // Arms the on_click_point seam (Object Lab smart select). While
    // armed a frame click emits on_click_point instead of toggling
    // playback — the old SetOnFrameClick arming rule.
    void SetClickPointsArmed(bool armed);
    bool ClickPointsArmed() const { return click_points_armed_; }
    // View-state report so composed chrome (zoom slider) tracks
    // wheel/double-click zoom changes without owning them.
    void SetOnZoomChanged(std::function<void(float)> cb);

    // ---- frame feed ----
    // Latest composited canvas, packed BGRA8 (copied) — the push path
    // (old MonitorWidget::SetExternalFrame parity).
    void SetFrame(const std::vector<std::uint8_t>& bgra, int width,
                  int height);
    // Zero-copy push path (chromium render-port R9): a refcounted
    // render::VideoFrame from the PreviewEngine pool. The surface
    // keeps the frame ref as the storage LEASE and aliases its pixel
    // vector for the paint/pull consumers — no copy anywhere; the
    // previous frame's storage recycles into the engine's pool when
    // this call drops its lease. CPU packed-BGRA/RGBA frames with
    // packed stride only; anything else (opaque texture, multi-planar,
    // padded stride) is ignored — the caller keeps its CPU fallback.
    void SetVideoFrame(Render::scoped_refptr<Render::VideoFrame> frame);
    // Pull path — consulted once per paint; may be nullptr.
    void SetFrameProvider(FrameProvider provider);
    bool HasFrame() const;
    int FrameWidth() const { return frame_width_; }
    int FrameHeight() const { return frame_height_; }
    // The last pushed frame as a shared snapshot (no copy) — the
    // scopes panel's frame source and the color eyedropper's sample
    // seam both read through this. Empty bgra when unset or when a
    // pull provider is the active feed (pulled frames aren't
    // retained).
    Frame CurrentFrame() const {
        return {frame_, frame_width_, frame_height_};
    }
    // One-line status drawn in place of the frame when none is
    // available ("Decoding…", "Audio only", errors).
    void SetStatusText(std::string text, bool is_error = false);

    // ---- document state pushed by the shell ----
    void SetTime(double seconds);           // playhead readout
    double Time() const { return time_; }
    void SetDuration(double seconds);       // timeline/media length
    double Duration() const { return duration_; }
    void SetWorkArea(double in_seconds, double out_seconds);
    void SetMarkers(const std::vector<Marker>& markers);
    void SetMarks(double in_seconds, double out_seconds);  // src marks
    std::pair<double, double> Marks() const {
        return {mark_in_, mark_out_};
    }
    // Shuttle-rate badge: the shell pushes the applied rate (0 or
    // exactly +1 hides; everything else — slow shuttle, reverse,
    // fast — paints a small "0.25x"/"-4x" chip on the video area).
    void SetSpeedBadge(double rate);
    double SpeedBadgeRate() const { return speed_rate_; }

    // ---- overlay toggles (surface view state) ----
    void SetSafeMargins(bool on);
    bool SafeMargins() const { return safe_margins_; }
    void SetGrid(bool on);
    bool Grid() const { return grid_; }
    void SetLoop(bool on);
    bool Loop() const { return loop_; }
    // Explicit loop range in seconds; out <= in clears it (falls back
    // to the work area / whole duration at draw time).
    void SetLoopRange(double in_seconds, double out_seconds);
    std::pair<double, double> LoopRange() const {
        return {loop_in_, loop_out_};
    }

    // ---- zoom / pan (surface view state) ----
    // Zoom multiplier over the letterboxed fit: 1.0 = fit, clamped to
    // [kMinZoom, kMaxZoom]. SetZoom(<=0) resets to fit.
    void SetZoom(float zoom);
    float Zoom() const { return zoom_; }
    void ZoomFit() { SetZoom(1.0f); }
    bool Zoomed() const { return zoom_ > 1.0f; }
    // Frame-normalized center being displayed (0.5,0.5 = centered).
    void SetViewCenter(float nx, float ny);
    std::pair<float, float> ViewCenter() const {
        return {view_cx_, view_cy_};
    }
    static constexpr float kMinZoom = 1.0f;   // never smaller than fit
    static constexpr float kMaxZoom = 16.0f;

    // ---- overlay inputs (lab/AI) ----
    // Frame-pixel markers drawn over the frame (pending smart-select
    // points). Empty clears.
    void SetMarkPoints(std::vector<std::pair<int, int>> frame_points);
    // Mask contour in FRAME pixel coordinates (spec S2 hook input) —
    // drawn as a closed accent polyline over the fitted frame. Empty
    // clears. Independent of SetMaskOverlay's coverage fill.
    void SetMaskContour(std::vector<Lab::IntPoint> frame_points);
    // Stored AI selection mask (w*h coverage bytes at frame
    // resolution): ~40% accent tint + traced boundary — old
    // SetMaskOverlay parity. nullptr clears; mismatched dims are
    // skipped, never misapplied.
    void SetMaskOverlay(
        std::shared_ptr<const std::vector<std::uint8_t>> mask,
        int width, int height);

    // ---- geometry (pure functions of GetBounds + view state) ----
    // The rect the frame paints into inside VideoAreaRect():
    // letterboxed at zoom=1, scaled about ViewCenter when zoomed.
    // Exported for hit mapping (spec S2).
    XER::UI::Layout::Rect FittedFrameRect() const;
    // The letterbox fit rect ignoring zoom/pan (zoom==1 dest).
    XER::UI::Layout::Rect FitRect() const;
    XER::UI::Layout::Rect VideoAreaRect() const;
    XER::UI::Layout::Rect RulerRect() const;
    // Maps a client point to frame-normalized [0,1] coordinates
    // through the CURRENT (zoomed/panned) frame rect; false outside.
    bool FrameNormPoint(XER::UI::Layout::Point p, float* nx,
                        float* ny) const;
    double RulerTimeAtX(float x) const;
    XER::UI::Widgets::Surface* KitSurface() const {
        return surface_.get();
    }

    // ---- transform gizmo (program monitor; spec §6 S2 addition) ----
    // Premiere/After-Effects direct manipulation: when the shell
    // binds MonitorHooks::gizmo_target and the PROGRAM surface has a
    // frame, the selected clip's transformed rect is drawn with 8
    // scale handles + a rotation stem + a center anchor. Gestures:
    //   * drag inside the box        — move (offset_x/offset_y)
    //   * corner/edge handle drag    — uniform scale about the
    //     anchor (Transform carries ONE `scale` — free aspect is
    //     not representable; edge drags scale by that axis's factor)
    //   * rotation stem drag         — rotation_deg about the anchor
    //     (Shift snaps to 15-degree steps)
    //   * press on another clip      — selects it via gizmo_pick +
    //     on_gizmo_select (topmost warped-rect hit test), then the
    //     same press arms a move drag on it
    //   * double-click inside box    — on_gizmo_reset (the shell
    //     restores the imported placement)
    //   * Esc mid-drag               — cancels: preview(orig) is
    //     re-fired, nothing commits
    // Snapping (gizmo_snap_enabled): move and edge-midpoint drags
    // snap the box's center/edges (move: rotated AABB) to frame
    // edges/center and the 90%/80% safe-margin lines; snapped guides
    // paint as accent indicator lines (timeline snap parity).
    //
    // Everything is hook-out: the surface never touches the document.
    // A drag fires on_gizmo_begin -> on_gizmo_preview* (in-memory
    // only) -> ONE on_gizmo_commit on release -> on_gizmo_end —
    // one undo step per gesture.
    bool GizmoVisible() const;
    bool GizmoDragging() const {
        return gizmo_mode_ != GizmoMode::None;
    }
    // Axis-aligned bbox of the gizmo box in CLIENT coordinates
    // (smoke/debug seam — empty rect when hidden).
    XER::UI::Layout::Rect GizmoBounds() const;

    // ---- widget contract ----
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(
        const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {240.0f, 160.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {640.0f, 400.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit MonitorSurface(Id id);

    void Paint(XER::UI::Rendering::Renderer& renderer,
               const XER::UI::Layout::Rect& content,
               const XER::UI::Theme::Tokens& tokens);
    bool OnSurfaceEvent(const XER::UI::Widgets::Event& event);
    void DrawOverlays(XER::UI::Rendering::Renderer& renderer,
                      const XER::UI::Theme::Tokens& tokens,
                      const XER::UI::Layout::Rect& dest);
    void DrawRuler(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens,
                   const XER::UI::Layout::Rect& ruler);
    void EnsureMaskOverlayBuilt(XER::UI::Theme::Color accent);
    // Effective loop range [in,out): explicit range, else work area,
    // else {0,0} meaning "whole duration".
    std::pair<double, double> EffectiveLoopRange() const;
    const Marker* ActiveMarker() const;
    void EmitStatus(const std::string& text, bool is_error = false);
    void Activate();   // focus + on_activate (last-active-viewer)

    // ---- transform gizmo internals ----
    enum class GizmoMode {
        None,
        Move,
        Rotate,
        ScaleNW, ScaleN, ScaleNE,
        ScaleW,               ScaleE,
        ScaleSW, ScaleS, ScaleSE,
    };
    // One gizmo geometry: the clip's warped rect in CLIENT coords.
    // `c` is the transform center (anchor — compositor rotates about
    // it); `pt[0..3]` the TL/TR/BR/BL corners; `rot` the rotation-
    // stem tip; `aabb` the client-space bounding box. The
    // UNROTATED half-size (pre-scale, pre-rotation: src*scale /2 in
    // frame px) is carried so scale drags can recompute the ratio
    // from pointer distance to center.
    struct GizmoGeom {
        float pt[4][2] = {};
        float c[2] = {};
        float rot[2] = {};
        float half_w = 0.0f;   // src*scale/2 mapped into client px
        float half_h = 0.0f;
        XER::UI::Layout::Rect aabb{};
    };
    // The hook-resolved target plus the geometry derived from it;
    // re-pulled per paint and at gesture start.
    struct GizmoState {
        MonitorGizmoTarget target;
        GizmoGeom geom;
        bool valid = false;
    };
    // Pulls hooks_.gizmo_target() and maps it through the CURRENT
    // frame rect into client coords. valid==false hides the box.
    GizmoState ResolveGizmo() const;
    bool GizmoGeomValid(const MonitorGizmoTarget& t) const;
    GizmoGeom GizmoGeometry(const MonitorGizmoTarget& t) const;
    void DrawGizmo(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens,
                   const GizmoState& s);
    GizmoMode GizmoHit(XER::UI::Layout::Point p,
                       const GizmoState& s) const;
    // Pointer-to-transform math for one drag frame; returns the
    // next effective transform (offset/scale/rotation only —
    // opacity untouched).
    Transform GizmoApplyDrag(XER::UI::Layout::Point p,
                             const XER::UI::Widgets::Event& e);
    // Move snap: the candidate's rotated-AABB edges+center snap to
    // frame edges/center/safe-margin lines; mutates the offset and
    // records the winning client-x/y guides for paint.
    void GizmoSnapMove(Transform* t);
    // Edge-drag snap: one AABB edge on one axis snaps to the same
    // line set; adjusts only that axis's offset (scale untouched).
    void GizmoSnapEdge(Transform* t, bool x_axis);
    void GizmoCancel();
    void GizmoFinish(bool commit);
    // Arms gizmo_mode_ = Move for clip `clip_id` after a
    // press-to-select; the target transform comes from a fresh
    // gizmo_target pull (selection round-trip). False when the pull
    // no longer resolves the clip.
    bool GizmoArmMove(const std::string& clip_id,
                      XER::UI::Layout::Point grab);

    // The gizmo target the last paint/drag resolved (kept so a drag
    // survives one dropped hook pull).
    MonitorGizmoTarget gizmo_target_;
    GizmoMode gizmo_mode_ = GizmoMode::None;
    Transform gizmo_orig_{};           // transform at drag start
    Transform gizmo_live_{};           // last previewed transform
    // Center<->pointer grab delta at drag start (frame-normalized)
    // so a move tracks by the grabbed point, not the box center.
    float gizmo_grab_dx_ = 0.0f, gizmo_grab_dy_ = 0.0f;
    // Rotate drags reuse the grab slot for the pointer's start angle
    // about the anchor (radians).
    // Gesture latching: gizmo_mode_ arms at press, but
    // on_gizmo_begin (the shell's macro + preview-compositor init)
    // only fires once the drag threshold is crossed — a plain click
    // on the box stays a click, never an empty gesture.
    bool gizmo_gesture_started_ = false;
    // Down-press that only claimed a gizmo hit/pick (no motion yet):
    // promotes to Move once the drag threshold is crossed.
    bool gizmo_pending_select_ = false;
    std::string gizmo_pending_clip_id_;
    // True while the current press was claimed by the gizmo path —
    // suppresses the synthesized Click's play toggle and feeds the
    // gizmo's own double-click-reset window.
    bool gizmo_click_ = false;
    double last_gizmo_click_at_ = -10.0;
    // Snap guides from the last move preview (client coords, -1 = none).
    float gizmo_snap_x_ = -1.0f, gizmo_snap_y_ = -1.0f;
    bool gizmo_dirty_commit_ = false;  // live != orig at release

    Id id_;
    MonitorHooks hooks_;
    std::function<void(float)> on_zoom_changed_;
    std::shared_ptr<XER::UI::Widgets::Surface> surface_;

    // Frame (push path, shared so CurrentFrame() snapshots are free)
    // + provider (pull path). For the VideoFrame feed, frame_ aliases
    // the frame's plane storage (SharePixels) and video_frame_ holds
    // the refcount lease — dropping the lease is what lets the pool
    // recycle the storage. frame_px_offset_ is the visible_rect
    // origin inside the packed plane (0 for today's producers, which
    // always render a full-frame visible rect).
    std::shared_ptr<const std::vector<std::uint8_t>> frame_;
    Render::scoped_refptr<Render::VideoFrame> video_frame_;
    std::size_t frame_px_offset_ = 0;
    int frame_width_ = 0;
    int frame_height_ = 0;
    FrameProvider frame_provider_;
    std::string status_text_ = "No video";
    bool status_error_ = false;

    // Document state pushed by the shell.
    double time_ = 0.0;
    double duration_ = 0.0;
    double work_in_ = 0.0, work_out_ = 0.0;
    double mark_in_ = -1.0, mark_out_ = -1.0;   // source marks
    double speed_rate_ = 0.0;                   // badge: 0/+1 hides
    std::vector<Marker> markers_;

    // Overlay toggles.
    bool safe_margins_ = false;
    bool grid_ = false;
    bool loop_ = false;
    double loop_in_ = 0.0, loop_out_ = 0.0;
    bool click_points_armed_ = false;

    // Zoom/pan view state: zoom multiplier over fit + the
    // frame-normalized point kept under the view center.
    float zoom_ = 1.0f;
    float view_cx_ = 0.5f, view_cy_ = 0.5f;

    // Drag state: left-press tracking for pan-vs-click and ruler scrub.
    bool left_down_ = false;
    bool drag_moved_ = false;
    bool ruler_drag_ = false;
    bool panning_ = false;
    XER::UI::Layout::Point down_pos_{};
    XER::UI::Layout::Point last_pan_pos_{};
    // Double-click synthesis (zoom-fit reset) — spec §6.6.
    double last_frame_click_at_ = -10.0;
    double click_clock_ = 0.0;   // advanced by Tick

    // AI/lab overlay inputs.
    std::vector<std::pair<int, int>> mark_points_;  // frame px
    std::vector<Lab::IntPoint> mask_contour_in_;     // frame px
    std::shared_ptr<const std::vector<std::uint8_t>> mask_overlay_;
    int mask_overlay_w_ = 0, mask_overlay_h_ = 0;
    std::vector<std::uint8_t> mask_bgra_;            // premul tint
    std::vector<Lab::IntPoint> mask_contour_;        // traced boundary
    const void* mask_built_for_ = nullptr;           // rebuild key
    float built_accent_[4] = {};
};

}  // namespace XER::VideoEditor::Ui2
