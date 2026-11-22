// TimelineStrip — the M2 timeline chrome: a widget-composed tool row
// (no painted controls) sitting above a real TimelineSurface.
// Replaces the M1 placeholder that lived in shell_widgets.
//
// Row contents (left to right, spec §6 strip):
//   * checkable tool-button group V/C/B/N/Y/U/R/H/Z — same order the
//     ShortcutRouter binds; one button is always checked and the
//     strip stays in sync with surface-level SetTool calls
//     (shortcut keys and the corner razor button both feed back
//     through TimelineSurface::SetOnToolChanged).
//   * SNAP checkable button — the single-source-of-truth flow is
//     shell -> SetSnapEnabled -> surface + button check.
//   * LINK checkable button — Premiere's "Linked Selection" toggle;
//     gates the Selection tool's link_group co-edit snapshot.
//   * zoom Slider (log-mapped to px/second, clamps 8..600 like the
//     wheel zoom) + a "fit" button (ZoomToFit over the project
//     duration).
//   * track-height Slider — scales the surface's compact lane.
#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "theme/tokens.h"
#include "timeline_surface.h"
#include "widgets/button.h"
#include "widgets/button_group.h"
#include "widgets/slider.h"
#include "widgets/widget.h"

namespace XER::VideoEditor::Ui2 {

class TimelineStrip final : public XER::UI::Widgets::Widget {
public:
    static std::shared_ptr<TimelineStrip> Create();

    // Shell-facing API (kept from the M1 placeholder so the
    // ShortcutRouter bindings are unchanged): tool indexes follow
    // the strip's V/C/B/N/Y/U/R/H/Z order.
    void SetTool(int index);
    int ActiveTool() const;
    void SetOnToolChanged(std::function<void(int)> cb);
    void SetOnSnapToggled(std::function<void(bool)> cb);
    void SetSnapEnabled(bool enabled);
    bool SnapEnabled() const;
    void SetLinkageEnabled(bool enabled);
    bool LinkageEnabled() const;

    // The hosted timeline surface — the shell pushes the Project,
    // playhead and media-change notifications through this.
    std::shared_ptr<TimelineSurface> Surface() const {
        return surface_;
    }
    // Legacy placeholder name kept for the shell's panel wiring.
    std::shared_ptr<XER::UI::Widgets::Widget> TracksSurface() const {
        return surface_;
    }

    void Render(XER::UI::Rendering::Renderer& renderer,
                const XER::UI::Theme::Tokens& tokens) override;
    void Layout(const XER::UI::Layout::Rect& bounds) override;
    using Widget::Layout;
    void Tick(double dt_seconds) override;
    bool HandleEvent(const XER::UI::Widgets::Event&) override {
        return false;
    }

    XER::UI::Layout::Size GetMinSize() const override;
    XER::UI::Layout::Size GetPreferredSize() const override;
    XER::UI::Layout::Size GetMaxSize() const override;

private:
    TimelineStrip() = default;

    // Slider<->surface zoom mapping (log scale — the wheel zoom's
    // multiplicative steps land evenly on the track).
    float ZoomToSlider(double px_per_second) const;
    double SliderToZoom(float value) const;
    void SyncToolChecks(ToolId active);

    std::shared_ptr<TimelineSurface> surface_;
    std::shared_ptr<XER::UI::Widgets::ButtonGroup> tool_group_;
    std::vector<std::shared_ptr<XER::UI::Widgets::Button>>
        tool_buttons_;
    std::shared_ptr<XER::UI::Widgets::Button> snap_button_;
    std::shared_ptr<XER::UI::Widgets::Button> link_button_;
    std::shared_ptr<XER::UI::Widgets::Slider> zoom_slider_;
    std::shared_ptr<XER::UI::Widgets::Button> fit_button_;
    std::shared_ptr<XER::UI::Widgets::Slider> height_slider_;

    bool snap_enabled_ = true;
    bool link_enabled_ = true;
    bool updating_checks_ = false;   // radio-sync recursion guard
    std::function<void(int)> on_tool_changed_;
    std::function<void(bool)> on_snap_toggled_;
    float tool_strip_height_ = 32.0f;
};

}  // namespace XER::VideoEditor::Ui2
