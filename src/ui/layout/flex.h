#ifndef XER_UI_LAYOUT_FLEX_H
#define XER_UI_LAYOUT_FLEX_H

#include <string_view>
#include <vector>

#include "geometry.h"
#include "../rendering/renderer.h"

namespace XER::UI::Layout {

// Alignment modes for distributing widgets inside a row or column container.
enum class FlexAlign {
    Start,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly
};

// Per-axis placement mode used by AlignedRect — the pure-geometry mirror
// of the Widgets::Alignment flags so layout helpers stay widget-agnostic.
enum class AlignMode {
    Fill,      // stretch to the cell extent (default/legacy behavior)
    Leading,   // pack at the cell start (left edge / top edge)
    Center,    // center inside the cell
    Trailing   // pack at the cell end (right edge / bottom edge)
};

// Positions `size` inside `cell` per axis. Fill adopts the cell extent;
// the other modes clamp to min(size, cell) and position accordingly.
// Used by Widget::SetBounds box-model resolution; tools laying out
// children by hand can call it to honor the same contract.
Rect AlignedRect(const Rect& cell, const Size& size,
                 AlignMode horizontal, AlignMode vertical) noexcept;

// Snaps a coordinate to the nearest physical pixel boundary for the given
// DPI scale (1.0 = 96 DPI). Prevents 1px borders and hairlines from blurring.
float Snap(float value, float scale = 1.0f) noexcept;

// Returns a Rect with x, y, width, and height snapped to the pixel grid.
Rect SnapRect(const Rect& rect, float scale = 1.0f) noexcept;

// Centers `size` inside `container` with optional horizontal/vertical padding.
Rect CenteredRect(const Rect& container, const Size& size,
                  float h_padding = 0.0f, float v_padding = 0.0f) noexcept;

// Returns a Rect centered vertically inside `container` with horizontal
// padding `h_padding`. When `center_horizontal` is true, the text is also
// centered horizontally within the padded container span.
Rect CenteredTextRect(Rendering::Renderer& renderer,
                      std::string_view text,
                      const Rendering::TextStyle& style,
                      const Rect& container,
                      float h_padding = 8.0f,
                      bool center_horizontal = false);

// Vertically centers an element of height `element_height` inside `container`.
Rect VerticallyCentered(const Rect& container, float element_height) noexcept;

// Distributes `base_widths` across `container` without overlap or subpixel
// gaps. In wide containers, extra space is distributed according to `align`
// and `grow_weights`. In narrow containers, items shrink proportionally to
// `base_widths` while preserving `gap` (reduced if necessary) and snapping
// every boundary to `scale`.
std::vector<Rect> DistributeRow(
    const Rect& container,
    const std::vector<float>& base_widths,
    const std::vector<float>& grow_weights = {},
    float gap = 0.0f,
    FlexAlign align = FlexAlign::Start,
    float scale = 1.0f);

} // namespace XER::UI::Layout

#endif // XER_UI_LAYOUT_FLEX_H
