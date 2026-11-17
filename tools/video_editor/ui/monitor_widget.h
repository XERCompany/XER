// Program monitor for the XER video editor. Draft playback uses the
// Phase-1C Compositor directly: every visible frame is the exact
// timeline composite (transforms/effects/transitions), not a proxy.
//
// Phase-2 parity additions (Premiere-style chrome):
//   * stereo audio meters on the right edge of the frame, fed by
//     SetPeakLevels() from the mixer/playback engine — instant attack,
//     ~20 dB/s falloff, peak-hold marks, green/yellow/red zones at
//     -12/-3 dBFS
//   * safe-margins overlay (action-safe 90% + title-safe 80% + center
//     cross) toggled by SetSafeMargins() or the on-monitor button
//   * rule-of-thirds grid overlay toggled by SetGrid()
//   * big HH:MM:SS:FF timecode readout (bottom-left) driven by the
//     playhead + project fps
//   * mini ruler strip: marker flags, work-area in/out brackets, loop
//     range band and the playhead tick; click to seek
//   * transport row: play/pause, safe-margins / grid / loop toggles and
//     a preview-quality badge (SetQualityTag, e.g. "1/2")
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "compositor.h"
#include "layout/geometry.h"
#include "timeline_model.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui {

class MonitorWidget final : public XER::UI::Widgets::Widget {
public:
    struct Hooks {
        std::function<void(double)> on_time_changed;
        std::function<void(const std::string&, bool)> on_status;
        // Focus-mode toggle: hide side panels so the monitor spans the
        // whole central row (the host restores them on a second hit).
        std::function<void()> on_toggle_focus;
    };

    explicit MonitorWidget(Hooks hooks = {});
    ~MonitorWidget() override;

    // Rebinds the exact project snapshot. Call after every timeline edit
    // — Compositor is intentionally snapshot-based.
    void SetProject(const Project& project);
    void SetTime(double seconds, bool force_render = false);
    // Alias for SetTime(seconds, force_render=true); kept for parity
    // naming with the spec/API docs ("set the playhead").
    void SetPlayhead(double seconds) { SetTime(seconds, true); }
    double Time() const { return time_; }
    void SetPlaying(bool playing);
    bool IsPlaying() const { return playing_; }

    // Push the latest per-channel peak levels (linear |sample| max,
    // typically 0..1) for the L/R meters. Call once per mixed chunk or
    // rendered frame; the widget applies ballistics (decay + peak hold).
    void SetPeakLevels(float left, float right);

    // Safe-margins overlay: action-safe (90%) + title-safe (80%)
    // outlines plus a center cross, drawn over the fitted frame.
    void SetSafeMargins(bool on);
    bool SafeMargins() const { return safe_margins_; }
    // Rule-of-thirds grid over the fitted frame.
    void SetGrid(bool on);
    bool Grid() const { return grid_; }
    // Loop playback. The looped range is SetLoopRange() when set, else
    // the project work area when set, else the whole timeline.
    void SetLoop(bool on);
    bool Loop() const { return loop_; }
    // Explicit loop range in seconds; out <= in clears it (falls back
    // to the work area / whole timeline).
    void SetLoopRange(double in_seconds, double out_seconds);
    std::pair<double, double> LoopRange() const {
        return {loop_in_, loop_out_};
    }
    // Preview-resolution badge ("Full", "1/2", "1/4"...). Empty hides it.
    void SetQualityTag(const std::string& tag);
    const std::string& QualityTag() const { return quality_tag_; }

    // External-playback mode: an outside engine (PreviewEngine) pumps
    // frames through SetExternalFrame — the internal compositor and the
    // Tick stepping loop stand down but the meter/overlay/transport
    // chrome keeps working. Turned off, the next Tick re-renders the
    // internal frame at the current time.
    void SetExternalPlayback(bool on);
    bool ExternalPlayback() const { return external_playback_; }
    // Latest externally-composited canvas, packed BGRA8 (copied).
    void SetExternalFrame(const std::vector<std::uint8_t>& bgra,
                          int width, int height);

    // When set, transport playback actions route to the app (PreviewEngine)
    // instead of the internal draft compositor. Unset = old local behavior.
    void SetOnPlayToggle(std::function<void()> cb);
    void SetOnSeekRequest(std::function<void(double time)> cb);
    void SetOnFrameStep(std::function<void(int dir)> cb);  // dir = -1|+1
    // Context-menu "Playback quality" pick: scale in (0,1] (Full=1,
    // 1/2, 1/4). The widget updates the badge/checkmark itself; the
    // host routes the scale to PreviewEngine::SetQuality.
    void SetOnQualityChange(std::function<void(float scale)> cb);
    // App reports engine playing state for button/context-menu labels.
    void SetEnginePlaying(bool playing);

    // Right-click overlay menu (playback + display toggles). Called by
    // the shell's tree context-menu handler. Returns true when shown.
    bool ShowContextMenu(XER::UI::Layout::Point position);

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const XER::UI::Widgets::Event& event) override;
    void Tick(double dt_seconds) override;

    XER::UI::Layout::Size GetMinSize() const override {
        return {360.0f, 280.0f};
    }
    XER::UI::Layout::Size GetPreferredSize() const override {
        return {720.0f, 480.0f};
    }
    XER::UI::Layout::Size GetMaxSize() const override {
        return {100000.0f, 100000.0f};
    }

private:
    // Overlay transport buttons, hit-tested in HandleEvent.
    enum class OverlayButton {
        Play,
        PrevFrame,
        NextFrame,
        SafeMargins,
        Grid,
        Loop,
        Focus
    };

    void RebuildCompositor();
    bool RenderCurrentFrame(std::string* error_out);
    void SetStatus(const std::string& text, bool is_error = false);
    void EmitTime(double t);
    // Play/pause label + glyph state: engine-reported while transport
    // hooks are wired, else the internal draft flag.
    bool TransportPlaying() const {
        return on_play_toggle_ ? engine_playing_ : playing_;
    }

    // Geometry helpers — pure functions of GetBounds() so hit-testing
    // works before the first Render.
    XER::UI::Layout::Rect VideoAreaRect() const;
    XER::UI::Layout::Rect RulerRect() const;
    XER::UI::Layout::Rect TransportRect() const;
    XER::UI::Layout::Rect OverlayButtonRect(OverlayButton button) const;
    // The rect the decoded frame is letterboxed into inside
    // VideoAreaRect() (the video_area itself when no frame).
    XER::UI::Layout::Rect FrameDestRect() const;
    // Effective loop range [in,out): explicit range, else project work
    // area, else {0,0} meaning "whole timeline".
    std::pair<double, double> EffectiveLoopRange() const;
    // Marker shown next to the ruler: latest marker at or before the
    // playhead, else nullptr.
    const Marker* ActiveMarker() const;
    double RulerTimeAtX(float x) const;

    void DrawOverlays(XER::UI::Rendering::Renderer& renderer,
                      const XER::UI::Theme::Tokens& tokens,
                      const XER::UI::Layout::Rect& dest);
    void DrawMeters(XER::UI::Rendering::Renderer& renderer,
                    const XER::UI::Theme::Tokens& tokens,
                    const XER::UI::Layout::Rect& video_area);
    void DrawTimecode(XER::UI::Rendering::Renderer& renderer,
                      const XER::UI::Theme::Tokens& tokens,
                      const XER::UI::Layout::Rect& video_area);
    void DrawRuler(XER::UI::Rendering::Renderer& renderer,
                   const XER::UI::Theme::Tokens& tokens,
                   const XER::UI::Layout::Rect& ruler);
    void DrawTransport(XER::UI::Rendering::Renderer& renderer,
                       const XER::UI::Theme::Tokens& tokens,
                       const XER::UI::Layout::Rect& row);
    void DrawOverlayButton(XER::UI::Rendering::Renderer& renderer,
                           const XER::UI::Theme::Tokens& tokens,
                           OverlayButton button, const char* label,
                           bool active);

    Hooks hooks_;
    Project project_;
    Compositor compositor_;
    bool compositor_ready_ = false;

    std::vector<std::uint8_t> frame_;
    int frame_width_ = 0;
    int frame_height_ = 0;
    std::string status_text_ = "No timeline frame";
    bool status_error_ = false;

    double time_ = 0.0;
    double rendered_time_ = -1.0;
    bool frame_dirty_ = true;
    bool playing_ = false;
    bool external_playback_ = false;
    double play_accumulator_ = 0.0;

    // App-driven transport hooks (PreviewEngine). While set, transport
    // actions emit these instead of mutating playing_/time_ locally.
    std::function<void()> on_play_toggle_;
    std::function<void(double)> on_seek_request_;
    std::function<void(int)> on_frame_step_;
    std::function<void(float)> on_quality_;
    bool engine_playing_ = false;

    // --- audio meters ---
    // Raw pushed peaks (linear) plus displayed dB values with ballistics.
    float peak_in_l_ = 0.0f;
    float peak_in_r_ = 0.0f;
    float meter_l_db_ = -60.0f;
    float meter_r_db_ = -60.0f;
    float peak_hold_l_db_ = -60.0f;
    float peak_hold_r_db_ = -60.0f;
    double peak_hold_l_secs_ = 0.0;
    double peak_hold_r_secs_ = 0.0;

    // --- overlays / transport ---
    bool safe_margins_ = false;
    bool grid_ = false;
    bool loop_ = false;
    double loop_in_ = 0.0;
    double loop_out_ = 0.0;
    std::string quality_tag_ = "Full";
};

}  // namespace XER::VideoEditor::Ui
