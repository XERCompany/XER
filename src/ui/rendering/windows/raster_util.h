#ifndef XER_UI_RENDERING_WINDOWS_RASTER_UTIL_H
#define XER_UI_RENDERING_WINDOWS_RASTER_UTIL_H

#include <cstdint>
#include <string_view>
#include <vector>

#include <windows.h>

#include "rendering/renderer.h"

namespace XER::UI::Rendering::Windows {

// Decodes an image file through WIC into straight (non-premultiplied)
// BGRA pixels — the layout DrawFrameBgra consumes. Returns false when the
// path cannot be decoded; `out_bgra`/`out_w`/`out_h` are untouched then.
bool DecodeImageFileToBgra(std::string_view path,
                           std::vector<std::uint8_t>* out_bgra,
                           int* out_w, int* out_h);

// GDI-backed text rasterizer shared by the software and Vulkan
// renderers (D2D uses DirectWrite instead). GDI cannot emit an alpha
// channel into a DIB, so text is drawn black-on-white and the pixel
// intensity is treated as glyph *coverage* — callers either alpha-blend
// it themselves or upload the mask and shade it as `1 - luminance`.
class GdiText {
public:
    // Measures `text` with a GDI font matching `style` at `dpi`.
    // `wrap_width_px` <= 0 means unconstrained (single line).
    static Layout::Size Measure(std::string_view text, TextStyle style,
                                unsigned dpi, float wrap_width_px = -1.0f);

    // Rasterizes `text` into a freshly allocated BGRA buffer sized
    // `width_px` x `height_px` (white background, black glyphs —
    // coverage = 1 - channel). IMPORTANT: `style.size` must already be
    // in device pixels (multiply the DIP size by dpi/96 before calling;
    // the mask has no separate DPI space). Returns false on empty text /
    // invalid rect / GDI failure.
    static bool RasterizeMask(std::string_view text, TextStyle style,
                              int width_px, int height_px,
                              std::vector<std::uint8_t>* out_bgra);

private:
    // Cached HFONT per (size_px, weight); fonts are GDI objects released
    // with the process — the cache intentionally never frees them.
    static HFONT FontFor(TextStyle style, unsigned dpi);
};

} // namespace XER::UI::Rendering::Windows

#endif // XER_UI_RENDERING_WINDOWS_RASTER_UTIL_H
