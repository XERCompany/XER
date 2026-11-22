#ifndef XER_UI_WIDGETS_WAVEFORM_WIDGET_H
#define XER_UI_WIDGETS_WAVEFORM_WIDGET_H

#include <chrono>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "widgets/widget.h"

namespace XER::UI::Widgets {

// Waveform strip with two modes:
//
//   * Peak mode (audio-ui/audiocn Waveform model): SetPeaks() feeds
//     {min,max} pairs per slice (the model every surveyed registry
//     uses — compute once per file at high resolution, the widget
//     resamples to width). SetRange() windows onto the source,
//     SetPosition() draws the playhead, SetRegions()/SetMarkers() add
//     tinted spans and cue lines, and the widget becomes interactive:
//     press jumps + drag scrubs (OnSeek), Shift+drag selects a range
//     (OnRangeSelect), wheel zooms while focused.
//
//   * Decorative mode (back-compat default when no peaks are set):
//     procedural bars driven by layered sines plus a per-column hash,
//     breathing with SetPlaying; SetProgress paints the played region
//     in the accent color. HandleEvent() stays inert, as before.
class WaveformWidget : public Widget {
public:
    enum class Shape { Mirror, Bars, Line };
    enum class RegionRole { Selection, Loop };

    struct Region {
        double start_s = 0.0;
        double end_s = 0.0;
        RegionRole role = RegionRole::Selection;
    };
    struct Marker {
        double time_s = 0.0;
        std::string label;
    };

    WaveformWidget();

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;
    // Tree-driven tick (Widget::Tick). Equivalent to the legacy no-arg
    // Tick() below; both share the same animation state.
    void Tick(double dt_seconds) override;

    Layout::Size GetMinSize() const override { return {80.0f, 18.0f}; }
    Layout::Size GetPreferredSize() const override { return {240.0f, 26.0f}; }
    Layout::Size GetMaxSize() const override { return {100000.0f, 100000.0f}; }

    // ---- Decorative-mode API (unchanged) ----
    void SetProgress(float progress);
    void SetPlaying(bool playing);
    // Legacy manual drive kept for AudioPlayerWidget and the addon
    // bindings; measures wall time itself and forwards to Tick(dt).
    void Tick();

    // ---- Peak-mode API ----
    // {min,max} pairs per slice in [-1,1]; duration_seconds maps the
    // slices onto the time domain (0 = normalized [0,1] domain).
    void SetPeaks(std::vector<std::pair<float, float>> peaks,
                  double duration_seconds = 0.0);
    void ClearPeaks();
    bool HasPeaks() const { return !peaks_.empty(); }
    // Visible window in the peak time domain; clamped to [0, duration].
    void SetRange(double start_s, double end_s);
    void SetShape(Shape shape);
    // Zoom helper in px/sec (wavesurfer minPxPerSec): fixes the visible
    // window length to width/px_per_sec, anchored at the range start.
    // Reapplied on Layout() once the widget has a real width.
    void SetMinPxPerSec(float px_per_sec);
    // Playhead time; outside the current range the playhead hides
    // (audio-ui `data-outside`) instead of pinning to an edge.
    void SetPosition(double time_s);
    double Position() const { return position_s_; }
    void SetRegions(std::vector<Region> regions);
    void SetMarkers(std::vector<Marker> markers);
    // Live seek: fires on press and while scrubbing.
    void SetOnSeek(std::function<void(double time_s)> callback);
    // Fires once on Shift+drag release with {start,end} ordered.
    void SetOnRangeSelect(std::function<void(double start_s, double end_s)> callback);

private:
    void Advance(float dt);
    void RenderPeaks(Rendering::Renderer& renderer,
                     const Theme::Tokens& tokens);
    void RenderDecorative(Rendering::Renderer& renderer,
                          const Theme::Tokens& tokens);
    std::pair<float, float> PeaksAt(double t0, double t1) const;
    float XFromTime(double t, const Layout::Rect& bounds) const;
    double TimeFromX(float x, const Layout::Rect& bounds) const;
    void ApplyMinPxPerSec();
    void SeekTo(float x, bool fire_callback);

    Layout::Rect bounds_;
    float progress_ = 0.0f;
    float amplitude_ = 0.15f;
    bool playing_ = false;
    float phase_ = 0.0f;
    std::chrono::steady_clock::time_point last_tick_;

    // ---- Peak-mode state ----
    std::vector<std::pair<float, float>> peaks_;
    double domain_end_s_ = 1.0;     // duration or 1.0 normalized
    double range_start_s_ = 0.0;
    double range_end_s_ = 1.0;
    double position_s_ = 0.0;
    Shape shape_ = Shape::Mirror;
    float px_per_sec_ = 0.0f;       // 0 = disabled
    std::vector<Region> regions_;
    std::vector<Marker> markers_;
    std::function<void(double)> on_seek_;
    std::function<void(double, double)> on_range_select_;
    bool scrubbing_ = false;
    bool selecting_ = false;
    double sel_anchor_s_ = 0.0;
    double sel_current_s_ = 0.0;
    float hover_x_ = -1.0f;         // widget-local x of the hover line
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_WAVEFORM_WIDGET_H
