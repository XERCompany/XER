#ifndef XER_UI_WIDGETS_GRADIENT_EDITOR_H
#define XER_UI_WIDGETS_GRADIENT_EDITOR_H

// Gradient stop editor — Colorshot GradientBar + AngleDial + ModeTabs as a
// single self-drawn widget:
//
//   [Linear|Radial|Conic]      type tabs
//   +---------------------+    preview band, rasterized from the gradient
//   |=====================|    (linear honors angle; radial/conic show the
//   +---------------------+    canonical sweep form)
//   --◆-------◆---------◆--    stop bar: drag stops along the track, click
//                              empty track to add a stop, drag a stop off
//                              the bar (or press Delete) to remove it
//        (angle dial)          for Linear: drag the dial handle to rotate
//
// Stops carry stable ids (Amplo model) so a controlled host can reconcile
// edits and bind a ColorPicker to the selected stop without positional
// ambiguity.
//
// Callback split matches ColorPicker: on_change fires per move while
// dragging, on_committed once at gesture end (drop / add / remove /
// type+angle button press).

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "theme/color.h"
#include "widgets/widget.h"

namespace XER::UI::Widgets {

enum class GradientType { Linear, Radial, Conic };

struct GradientStop {
    std::uint32_t id = 0;
    Theme::Color color{};
    float position = 0.0f;   // 0..1 along the gradient line
};

struct Gradient {
    GradientType type = GradientType::Linear;
    // Direction of the gradient line, CSS convention: 0deg points up
    // (bottom-to-top), 90deg points right (left-to-right), increasing
    // clockwise.
    float angle_deg = 90.0f;
    std::vector<GradientStop> stops;
};

class GradientEditor final : public Widget {
public:
    // Stop interpolation space for the preview band. Srgb matches legacy
    // gradients; Oklch gives CSS Color 4-style perceptual interpolation
    // (no muddy midpoint between complementary hues).
    enum class Interpolation { Srgb, Oklch };

    GradientEditor();

    void SetGradient(Gradient gradient);
    const Gradient& GetGradient() const { return gradient_; }
    void SetType(GradientType type);
    void SetAngle(float degrees);
    void SetInterpolation(Interpolation interp);
    Interpolation GetInterpolation() const { return interp_; }

    // Stop management (stable ids; AddStop returns the new id, 0 on
    // failure — e.g. position duplication beyond resolution).
    std::uint32_t AddStop(Theme::Color color, float position);
    bool RemoveStop(std::uint32_t id);
    bool SetStopColor(std::uint32_t id, Theme::Color color);
    bool SetStopPosition(std::uint32_t id, float position);
    // The stop most recently clicked/dragged — pair with an external
    // ColorPicker via SetOnStopSelected.
    std::optional<std::uint32_t> SelectedStopId() const;
    void SelectStop(std::uint32_t id);

    void SetOnGradientChange(std::function<void(const Gradient&)> callback);
    void SetOnGradientCommitted(
        std::function<void(const Gradient&)> callback);
    // Fired when selection changes (click/drag-select of a stop), 0 when
    // nothing is selected.
    void SetOnStopSelected(std::function<void(std::uint32_t id)> callback);

    // Sample the gradient at position t (0..1) in the active interpolation
    // space. Exposed for hosts painting the same ramp elsewhere.
    Theme::Color SampleAt(float t) const;

    void Render(Rendering::Renderer& renderer, const Theme::Tokens& tokens) override;
    void Layout(const Layout::Rect& bounds) override;
    using Widget::Layout;
    bool HandleEvent(const Event& event) override;

    Layout::Size GetMinSize() const override;
    Layout::Size GetPreferredSize() const override;
    Layout::Size GetMaxSize() const override;

private:
    struct Chrome {
        Layout::Rect tabs;
        Layout::Rect preview;
        Layout::Rect stop_bar;     // track the stop handles ride on
        Layout::Rect dial;         // square cell for the angle dial
        Layout::Rect angle_row;    // dial + numeric readout row
    };
    enum class Drag { None, Stop, Dial };

    Chrome GetChrome() const;
    float StopBarT(float x) const;
    Layout::Point StopHandlePos(const GradientStop& stop) const;
    std::optional<std::uint32_t> HitStop(const Layout::Point& p) const;
    void ApplyEdit(bool live);
    void Commit();
    void SelectInternal(std::optional<std::uint32_t> id);
    std::uint32_t NextId();
    void RasterizePreview(int w, int h,
                          std::vector<std::uint8_t>* out) const;
    Theme::Color SampleSrgb(float t) const;
    Theme::Color SampleOklch(float t) const;

    Gradient gradient_;
    Interpolation interp_ = Interpolation::Srgb;
    std::uint32_t next_id_ = 1;
    std::optional<std::uint32_t> selected_;

    Drag drag_ = Drag::None;
    std::uint32_t drag_id_ = 0;
    bool drag_removed_ = false;    // stop dragged off the bar this gesture

    bool preview_dirty_ = true;
    std::vector<std::uint8_t> preview_raster_;
    int preview_raster_w_ = 0, preview_raster_h_ = 0;

    std::function<void(const Gradient&)> on_change_;
    std::function<void(const Gradient&)> on_committed_;
    std::function<void(std::uint32_t)> on_stop_selected_;
};

} // namespace XER::UI::Widgets

#endif // XER_UI_WIDGETS_GRADIENT_EDITOR_H
