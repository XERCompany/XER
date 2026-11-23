// MonitorPanel — the widget-composed chrome AROUND a MonitorSurface
// (spec §5/§6 S2): everything the old MonitorWidget drew as one
// monolithic widget, split per the rebuild rule that chrome stays
// widgets and only the frame+overlays live on a surface.
//
//   ┌ header ─ title Label ····· proxy Badge · quality Badge ┐
//   │ MonitorSurface (frame+overlays+ruler) │ LevelMeter L/R │
//   └ transport row: mono timecode |◄ ‹ ►/❚❚ ■ › ►| ─ edit ─┘
//     cluster ─ safe/grid/loop toggles ─ quality Select ─ zoom
//
// Per-scope edit cluster (old-app parity):
//   Source  — IN / OUT marks + INS / OVR three-point edits
//   Program — IN / OUT marks (program in/out → work area) + LIFT /
//             EXT range edits
//
// The panel owns NO transport state of its own: every button fires a
// Hooks member the shell binds to the PreviewEngine path (old
// MonitorWidget::SetOnPlayToggle / SourceMonitorWidget::Hooks
// contract, flattened into one struct — unset members mean the
// gesture is inert, never a silent mutation).
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "monitor_surface.h"
#include "timeline_model.h"   // Marker
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Badge;
class Button;
class Label;
class LevelMeter;
class Select;
class Slider;
}
namespace XER::VideoEditor::Lab {
struct IntPoint;
}

namespace XER::VideoEditor::Ui2 {

class MonitorPanel final : public XER::UI::Widgets::Widget {
public:
    enum class Kind { Program, Source };

    // Stable ids for the smoke harness / a11y (Chrome() lookup).
    enum class ChromeId {
        ToStart, PrevFrame, Play, Stop, NextFrame, ToEnd,
        MarkIn, MarkOut,
        Insert, Overwrite,   // Source scope
        Lift, Extract,       // Program scope
        SafeMargins, Grid, Loop,
        QualitySelect, ZoomSlider,
        Timecode, Title, Meters,
    };

    struct Hooks {
        // Fired on any press inside the panel — the shell routes the
        // shortcut scope (last-active-viewer rule → ActivateScope).
        std::function<void()> on_activate;
        std::function<void()> on_play_toggle;
        // Stop = pause + reset the shuttle ladder at the playhead
        // (the panel keeps a dedicated ■ unlike the old widget,
        // which folded this into the play toggle).
        std::function<void()> on_stop;
        std::function<void(int dir)> on_frame_step;   // -1|+1
        std::function<void()> on_to_start;
        std::function<void()> on_to_end;
        // Surface ruler scrub (MonitorHooks::on_time_changed).
        std::function<void(double)> on_seek;
        // Preview scale pick: 1.0 / 0.5 / 0.25.
        std::function<void(float)> on_quality;
        // Requested overlay state — the shell stays the source of
        // truth and calls the matching Setters back (same
        // snap-toggle discipline as TimelineHooks::on_snap_toggle).
        std::function<void(bool)> on_safe_toggled;
        std::function<void(bool)> on_grid_toggled;
        std::function<void(bool)> on_loop_toggled;
        std::function<void()> on_mark_in;
        std::function<void()> on_mark_out;
        std::function<void()> on_insert;      // , edit (Source)
        std::function<void()> on_overwrite;   // . edit (Source)
        std::function<void()> on_lift;        // range lift (Program)
        std::function<void()> on_extract;     // range extract (Program)
        std::function<void(float, float)> on_click_point;
        std::function<void(const std::string&, bool)> on_status;
    };

    static std::shared_ptr<MonitorPanel> Create(
        Kind kind = Kind::Program);

    Kind GetKind() const { return kind_; }
    void SetHooks(Hooks hooks);

    // The frame surface — shells push frames/document state through
    // it directly where the panel has no mirror (mask, marks strip).
    MonitorSurface& VideoSurface() { return *surface_; }
    const MonitorSurface& VideoSurface() const { return *surface_; }

    // Chrome lookup for tests/a11y; nullptr for scope-hidden ids
    // (Insert on a Program panel and vice versa).
    XER::UI::Widgets::Widget* Chrome(ChromeId id) const;

    // ---- pushed state (shell -> panel) ----
    void SetTitle(std::string title);
    void SetFps(int num, int den);          // timecode + frame step
    void SetTimecode(double seconds);
    void SetDuration(double seconds);
    void SetPlaying(bool playing);
    bool IsPlaying() const { return playing_; }
    // Linear peak magnitudes (0..1+) — converted to dBFS for the
    // LevelMeter feed; ballistics live inside the widget.
    void SetPeakLevels(float left, float right);
    // Preview-scale pick sync (1.0/0.5/0.25 → select index + badge).
    void SetQualityScale(float scale);
    float QualityScale() const { return quality_scale_; }
    // Free-form chip override ("PRX", "1/2"…); empty hides.
    void SetProxyBadge(std::string text);
    void SetStatusText(std::string text, bool is_error = false);
    // Overlay-state mirrors (the shell pushes the requested state
    // back so the buttons stay honest).
    void SetSafeMargins(bool on);
    void SetGrid(bool on);
    void SetLoopEnabled(bool on);
    void SetLoopRange(double in_seconds, double out_seconds);
    void SetWorkArea(double in_seconds, double out_seconds);
    void SetMarkers(const std::vector<Marker>& markers);
    void SetMarks(double in_seconds, double out_seconds);
    std::pair<double, double> Marks() const;
    void SetFrame(const std::vector<std::uint8_t>& bgra, int width,
                  int height);
    void SetClickPointsArmed(bool armed);
    void SetMarkPoints(std::vector<std::pair<int, int>> frame_points);
    void SetMaskContour(std::vector<Lab::IntPoint> frame_points);
    void SetMaskOverlay(
        std::shared_ptr<const std::vector<std::uint8_t>> mask,
        int width, int height);

    // ---- widget contract ----
    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(
        const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {320.0f, 240.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {720.0f, 480.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    explicit MonitorPanel(Kind kind);

    void BuildChildren();
    void Activate();   // focus + hooks_.on_activate
    void SyncPlayButton();
    void SyncToggleButtons();
    // Geometry — pure functions of GetBounds().
    XER::UI::Layout::Rect HeaderRect() const;
    XER::UI::Layout::Rect TransportRect() const;
    XER::UI::Layout::Rect BodyRect() const;
    XER::UI::Layout::Rect MetersRect() const;
    float CenterStripLeft(float row_width) const;
    XER::UI::Layout::Rect ChromeRect(ChromeId id) const;

    Kind kind_;
    Hooks hooks_;

    std::shared_ptr<MonitorSurface> surface_;
    std::shared_ptr<XER::UI::Widgets::LevelMeter> meter_;
    std::shared_ptr<XER::UI::Widgets::Label> title_label_;
    std::shared_ptr<XER::UI::Widgets::Badge> proxy_badge_;
    std::shared_ptr<XER::UI::Widgets::Badge> quality_badge_;
    std::shared_ptr<XER::UI::Widgets::Widget> timecode_;

    std::shared_ptr<XER::UI::Widgets::Button> to_start_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> prev_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> play_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> stop_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> next_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> to_end_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> mark_in_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> mark_out_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> insert_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> overwrite_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> lift_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> extract_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> safe_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> grid_btn_;
    std::shared_ptr<XER::UI::Widgets::Button> loop_btn_;
    std::shared_ptr<XER::UI::Widgets::Select> quality_select_;
    std::shared_ptr<XER::UI::Widgets::Slider> zoom_slider_;

    bool playing_ = false;
    bool loop_enabled_ = false;
    float quality_scale_ = 1.0f;
    bool syncing_quality_ = false;
    bool syncing_zoom_ = false;
    int fps_num_ = 30, fps_den_ = 1;
    double timecode_seconds_ = 0.0;
};

}  // namespace XER::VideoEditor::Ui2
