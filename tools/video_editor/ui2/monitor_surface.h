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
    // Pull path — consulted once per paint; may be nullptr.
    void SetFrameProvider(FrameProvider provider);
    bool HasFrame() const;
    int FrameWidth() const { return frame_width_; }
    int FrameHeight() const { return frame_height_; }
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

    Id id_;
    MonitorHooks hooks_;
    std::function<void(float)> on_zoom_changed_;
    std::shared_ptr<XER::UI::Widgets::Surface> surface_;

    // Frame (push path) + provider (pull path).
    std::vector<std::uint8_t> frame_;
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
