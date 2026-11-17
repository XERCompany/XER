// Generated clip sources — the procedural "no decoder" counterparts of
// Premiere's Color Matte / Bars & Tone / Title clips. A Clip whose
// source.kind != Media materializes its intrinsic frame through
// GenerateSourceFrame instead of opening an IMediaDecoder; the
// compositor then runs the SAME effects -> masks -> warp -> blend
// stages it applies to decoded frames, so transforms/keyframes work
// identically on generated content.
//
// Natural sizes: ColorMatte and ColorBars fill the PROJECT canvas
// (their Transform rect then covers the canvas 1:1, like a matte
// should); Text renders at its measured size so Transform.scale is a
// real zoom (glyph scale derives from canvas height — see
// TextClipGlyphScale — so a title stays proportionate to the canvas).
//
// All pixels are packed BGRA8 STRAIGHT alpha, matching the compositor
// pipeline contract.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "frame.h"
#include "timeline_model.h"

namespace XER::VideoEditor {

// True for every procedural kind (ColorMatte, ColorBars, Text — i.e.
// anything that must NOT open a decoder session).
inline bool IsGeneratedSource(const ClipSource& source) {
    return source.kind != SourceKind::Media;
}

// Lenient matte hex parser: accepts "#RRGGBB"/"#RRGGBBAA" (the '#'
// optional, hex case-insensitive) and returns {b,g,r,a}. Anything
// malformed — empty, wrong digit count, non-hex chars — yields opaque
// black (Premiere's default matte is black too).
std::array<std::uint8_t, 4> ParseMatteColor(const std::string& param);

// The 7 SMPTE 75% bar colors, left->right, as {b,g,r,a} — exposed so
// tests/UI previews share the exact table.
const std::array<std::array<std::uint8_t, 4>, 7>& Smpte75Bars();

// Default glyph scale for Text clips: proportionate to the canvas
// (one line ≈ canvas_h/17) clamped to [1, 64]. 1080p -> 8.
int TextClipGlyphScale(int canvas_h);

// ---- text clip styling ----

// Inline style for Text clips, parsed out of Clip::source.param:
//   "<text> | size=<px> color=#RRGGBB[AA] outline=<px>
//            outline_color=#RRGGBB[AA] align=left|center|right"
// The FIRST '|' separates the text payload from whitespace-separated
// key=value tokens — a literal '|' cannot appear inside the text.
// Unknown keys and malformed values are ignored (a style typo must
// never kill the title). With no '|' the whole param is the text and
// ClipTextStyle{} applies — which is exactly the pre-style look
// (white face + soft drop shadow at the canvas-proportional scale),
// so old projects render unchanged.
struct ClipTextStyle {
    enum class Align { Left, Center, Right };
    std::string text;           // payload before '|', r-trimmed
    int size_px = 0;            // 0 -> canvas-proportional default;
                                // else nominal glyph height in px —
                                // cell scale = round(size_px /
                                // BitmapFont::kGlyphHeight), [1,64]
    std::array<std::uint8_t, 4> color{255, 255, 255, 255};        // BGRA face
    float outline_px = 0.0f;    // >0 -> stroke replaces the drop shadow
    std::array<std::uint8_t, 4> outline_color{0, 0, 0, 255};      // BGRA
    Align align = Align::Left;  // per-line, inside the measured block
};

// Parses Clip::source.param for a Text clip (see above). Never fails —
// the result always carries a usable text payload and defaults.
ClipTextStyle ParseClipTextStyle(const std::string& param);

// Produces the clip's intrinsic frame at clip-local time
// `clip_local_t` (currently every generated kind is time-invariant —
// the parameter exists for future animated sources). `canvas_w`/
// `canvas_h` are the PROJECT canvas dims: matte/bars adopt them as
// their natural size, text uses them only for glyph scale and as a
// size ceiling. `out` is (re)allocated to the source's natural size —
// reuse across calls keeps the buffer. Returns false on invalid
// inputs (bad canvas, unknown kind, allocation failure); an EMPTY
// Text param still succeeds with a 1x1 transparent frame.
bool GenerateSourceFrame(const Clip& clip, double clip_local_t,
                         int canvas_w, int canvas_h, video::Frame* out);

}  // namespace XER::VideoEditor
