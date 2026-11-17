// Mixer panel: Premiere-style audio track mixer — one channel strip
// per AUDIO track plus a master strip at the right.
//
//   ┌────────┐ ┌────────┐ ┌────────┐
//   │ [M] (o)│ │ [M] (o)│ │        │   M = mute chip, (o) = pan knob
//   │  |  || │ │  |  || │ │  |  || │   | = gain fader, || = L/R meters
//   │  |  || │ │  |  || │ │  |  || │
//   │ +0.0dB │ │ -3.5dB │ │ +0.0dB │
//   │  A1    │ │  A2    │ │ Master │
//   └────────┘ └────────┘ └────────┘
//
// Everything is drawn + hit-tested by the panel itself (Slider is
// horizontal-only, so the gain fader is a custom vertical drag rect;
// the pan knob drags horizontally, −1..1). Double-click on the fader or
// the pan knob resets it to 0 dB / center. The mouse wheel scrolls the
// strip row horizontally when tracks overflow.
//
// Strips mirror Track::gain_db / Track::muted on SetProject; per-track
// pan is NOT in the model — the panel keeps it locally and still emits
// on_track_pan so the host can route/store it as it sees fit. The
// master strip's fader emits on_master_gain (no model field either);
// push the authoritative value back via SetMasterGainDb().
//
// Meters are fed by SetPeak() with linear |sample| maxima (may exceed
// 1.0 on overs — the bars clamp to the -60..0 dB display and light red
// above -3 dB). Instant attack, ~20 dB/s falloff, 1.6 s peak-hold —
// the same ballistics as MonitorWidget::SetPeakLevels.
#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {
class Label;
}

namespace XER::VideoEditor::Ui {

class MixerPanelWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        // `track` is the index into Project::tracks (not the strip
        // order). Emitted continuously while the fader drags — the
        // host decides history granularity (e.g. one macro per drag).
        std::function<void(int track, float db)> on_track_gain;
        std::function<void(int track, float pan)> on_track_pan;
        std::function<void(int track, bool muted)> on_track_mute;
        // Master fader writes here (no track index — the mix bus).
        std::function<void(float db)> on_master_gain;
    };

    explicit MixerPanelWidget(Hooks hooks = {});

    // `project` may be nullptr (clears all strips). Rebuilds the strip
    // row from the project's AUDIO tracks + master — call again after
    // track add/remove/mute/gain changes to refresh.
    void SetProject(const Project* project);

    // Live meter feed: `strip` is the strip order (0 = leftmost audio
    // track); -1 OR the strip count selects the master. Values are
    // linear peaks, not dB.
    void SetPeak(int strip, float l, float r);

    // Syncs the master fader position (e.g. after load/undo).
    void SetMasterGainDb(float db);
    float MasterGainDb() const { return master_gain_db_; }
    // Pan last shown on a track strip, in case the host persists it
    // out-of-model. 0 when untouched.
    float TrackPan(int track_index) const;
    int StripCount() const { return static_cast<int>(strips_.size()); }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {220.0f, 180.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {520.0f, 240.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    struct Strip {
        int track_index = -1;   // Project::tracks index; -1 = master
        std::string name;
        float gain_db = 0.0f;
        float pan = 0.0f;       // -1..1; local only (not in the model)
        bool muted = false;
        bool master = false;
        // Raw pushed peaks (linear) + displayed dB with ballistics.
        float peak_in_l = 0.0f;
        float peak_in_r = 0.0f;
        float meter_l_db = -60.0f;
        float meter_r_db = -60.0f;
        float hold_l_db = -60.0f;
        float hold_r_db = -60.0f;
        double hold_l_secs = 0.0;
        double hold_r_secs = 0.0;
    };

    enum class DragMode { None, Fader, Pan };

    void EnsureChildren();
    void RebuildStrips(const Project* project);
    XER::UI::Layout::Rect StripRect(int strip) const;
    // Sub-rects inside a strip (hit-testing + drawing share them).
    XER::UI::Layout::Rect FaderTrackRect(
        const XER::UI::Layout::Rect& strip) const;
    XER::UI::Layout::Rect PanRect(
        const XER::UI::Layout::Rect& strip) const;
    XER::UI::Layout::Rect MuteRect(
        const XER::UI::Layout::Rect& strip) const;
    XER::UI::Layout::Rect MeterRect(
        const XER::UI::Layout::Rect& strip) const;
    int StripAt(const XER::UI::Layout::Point& position) const;
    float DbFromFaderY(float y,
                       const XER::UI::Layout::Rect& track) const;
    float FaderYFromDb(float db,
                       const XER::UI::Layout::Rect& track) const;
    void SetStripGain(int strip, float db);
    void SetStripPan(int strip, float pan);
    void DrawStrip(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens, int strip,
                   const XER::UI::Layout::Rect& rect) const;
    float ScrollRange() const;

    Hooks hooks_;
    std::vector<Strip> strips_;
    // Survives RebuildStrips: Track has no pan field, so pan is keyed
    // by track index out-of-model.
    std::map<int, float> pan_by_track_;
    float master_gain_db_ = 0.0f;
    float scroll_x_ = 0.0f;
    float strips_top_ = 0.0f;         // layout: y where strips begin
    float strips_bottom_ = 0.0f;      // layout: strips viewport bottom
    int hovered_strip_ = -1;
    // Drag state (fader + pan share it; only one active at a time).
    int drag_strip_ = -1;
    DragMode drag_mode_ = DragMode::None;
    float pan_drag_start_ = 0.0f;
    float pan_drag_x_ = 0.0f;
    float drag_start_db_ = 0.0f;
    // A drag that actually moved ends as a Click too — swallow it so
    // two quick drags can't be misread as the reset double-click.
    bool just_dragged_ = false;
    // Double-click-to-reset detection (same window as sibling panels).
    int last_click_strip_ = -1;
    DragMode last_click_mode_ = DragMode::None;
    std::chrono::steady_clock::time_point last_click_time_{};
    bool children_attached_ = false;

    std::shared_ptr<XER::UI::Widgets::Label> title_;
    std::shared_ptr<XER::UI::Widgets::Label> hint_;
};

}  // namespace XER::VideoEditor::Ui
