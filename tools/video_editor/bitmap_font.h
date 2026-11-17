// Compact built-in 5x7 bitmap font for generated Text clips (the
// Premiere "title" equivalent). Chosen over Skia text deliberately:
// the Skia bridge may be absent in headless builds and even when
// present it needs font files whose availability we cannot guarantee
// inside the core lib. A self-contained bitmap font keeps Text clips
// rendering identically on every build/config.
//
// Coverage: printable ASCII 0x20..0x7E. UTF-8 input is decoded to
// codepoints; anything outside the table (multibyte accents, CJK,
// malformed bytes) resolves to the '?' glyph so arbitrary UTF-8 text
// never crashes or draws garbage. '\n' is a line break handled by the
// measure/draw helpers — it is NOT passed through the glyph table.
//
// All metrics are integers in units of one "cell" (kGlyphWidth x
// kGlyphHeight); callers multiply by an integer scale factor >= 1.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace video {
class Frame;
}  // namespace video

namespace XER::VideoEditor::BitmapFont {

constexpr int kGlyphWidth = 5;     // set bits live in the low 5 bits
constexpr int kGlyphHeight = 7;    // rows per glyph
constexpr int kGlyphAdvance = 6;   // cell + 1px tracking, per column
constexpr int kLineAdvance = 9;    // cell + 2px leading, per line

// 7 row bytes for `codepoint`: bit 4 (0x10) is the LEFTMOST pixel
// column, bit 0 the rightmost; bits 5..7 are always 0. Codepoints
// outside 0x20..0x7E return the '?' glyph.
const std::uint8_t* GlyphRows(char32_t codepoint);

// Measures `utf8` at integer `scale` (>= 1): the tight box around the
// drawn cells — width drops the trailing tracking column of the
// longest line, height drops the trailing leading of the last line.
// "A" at scale 1 -> 5x7; "A\nB" -> 5x16. Empty text -> 0x0.
// Returns false on null outputs or scale < 1.
bool MeasureText(const std::string& utf8, int scale, int* out_w,
                 int* out_h);

// Draws `utf8` into `frame` (packed BGRA8, straight alpha) with the
// top-left glyph cell at pixel (origin_x, origin_y) — pixels outside
// the frame are clipped. Each set glyph bit paints a scale*scale
// block alpha-overed with `bgra` ({b,g,r,a}) — so layered passes
// (shadow then face) composite correctly. No-op on invalid frames,
// empty text, or scale < 1.
void DrawText(video::Frame* frame, const std::string& utf8,
              int origin_x, int origin_y, int scale,
              const std::array<std::uint8_t, 4>& bgra);

}  // namespace XER::VideoEditor::BitmapFont
