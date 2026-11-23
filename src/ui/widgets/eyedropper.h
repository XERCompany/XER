#ifndef XER_UI_WIDGETS_EYEDROPPER_H
#define XER_UI_WIDGETS_EYEDROPPER_H

// Eyedropper — screen-sample color picking.
//
// SCOPE (honest): the widget is a *surface overlay*, not a global screen
// grabber. It is laid out over the region it may sample (typically the
// monitor/canvas widget's bounds); the host provides the pixel source via
// SetSampleSource() — in-canvas content this is a small closure over the
// current video frame + Lab::SampleRegionAvg (tools/video_editor/lab/
// color_pick.h already does BGRA region averaging). The magnifier draws
// from the same source, so what you see in the loupe is exactly what the
// pick returns.
//
// Global desktop capture (BitBlt of GetDC(NULL)) is NOT wired in: the
// widget's coordinate space is client-space and it cannot map to screen
// coordinates without a platform/window hook. A host that wants
// whole-screen picking can still provide it — the SampleFn contract takes
// client coordinates and the host can BitBlt behind it — but no built-in
// default exists until the window<->screen mapping lands in platform/.
//
// Flow: SetActive(true) -> magnifier follows the cursor -> left click
// samples + fires on_pick + auto-deactivates; Escape fires on_cancel and
// deactivates without picking.

#include <functional>

#include "layout/geometry.h"
#include "theme/color.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

class Eyedropper final : public Widget {
public:
    // Maps a client-coordinate point to a color. Without a source the
    // widget stays inert (loupe shows "no source" and clicks do nothing).
    using SampleFn = std::function<Theme::Color(float x, float y)>;

    Eyedropper();

    void SetSampleSource(SampleFn fn);
    bool HasSampleSource() const { return static_cast<bool>(sample_); }

    // Region averaging radius in source pixels: 0 = single sample, N = mean
    // over the (2N+1)^2 window around the cursor — the same knob
    // Lab::SampleRegionAvg exposes, applied here over SampleFn calls.
    void SetSampleRadius(int radius_px);
    int GetSampleRadius() const { return sample_radius_; }

    // Magnifier geometry: cells x cells grid, each cell mag px.
    void SetMagnifier(int cells = 11, int cell_px = 9);
    void SetShowHexReadout(bool show) { show_hex_ = show; }

    void SetActive(bool active);
    bool IsActive() const { return active_; }

    // Fires once with the picked sRGB color on left click.
    void SetOnPick(std::function<void(Theme::Color)> callback);
    // Fires on Escape (or right-click) cancel — no pick is emitted.
    void SetOnCancel(std::function<void()> callback);
    // Optional live hook: every sampled color under the cursor while
    // active (useful to echo the hovered value in a status bar).
    void SetOnHoverSample(std::function<void(Theme::Color)> callback);

    // The color under cursor_ right now (region-averaged). Returns
    // transparent when inactive or without a source.
    Theme::Color CurrentSample() const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    Theme::Color SampleAt(const Layout::Point& p) const;
    Layout::Rect LoupeRect() const;

    SampleFn sample_;
    int sample_radius_ = 1;
    int mag_cells_ = 11;
    int mag_cell_px_ = 9;
    bool show_hex_ = true;
    bool active_ = false;
    bool cursor_valid_ = false;
    Layout::Point cursor_{};

    std::function<void(Theme::Color)> on_pick_;
    std::function<void()> on_cancel_;
    std::function<void(Theme::Color)> on_hover_sample_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_EYEDROPPER_H
