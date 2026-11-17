// Minimal SVG rasterizer for Material-style icon files
// (<svg viewBox="0 0 24 24"><path d="..."/></svg>) — the shape of the
// assets/icons pack. Parses <path d> data (M L H V C S Q T A Z,
// absolute + relative), flattens curves to polylines, and
// scanline-fills with the nonzero winding rule into a BGRA8 bitmap
// tinted a uniform color. Not a general SVG engine — gradients,
// strokes and multi-shape files degrade gracefully (every <path> is
// filled with the tint color; <rect>/<circle>/<ellipse> also filled).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace XER::VideoEditor::AppRuntime {

// Renders `svg_utf8` into a square out_size×out_size BGRA bitmap
// (4 bytes/px, B,G,R,A — feedable to Renderer::DrawFrameBgra).
// `argb` tints every filled shape (source fill colors are ignored —
// icon packs are monochrome by convention). Aspect ratio preserved:
// the viewBox is scaled to fit out_size centered.
// Returns false on unparseable input; *bgra is left untouched.
bool RasterizeSvgIcon(const std::string& svg_utf8, int out_size,
                      std::uint32_t argb, std::vector<std::uint8_t>* bgra);

}  // namespace XER::VideoEditor::AppRuntime
