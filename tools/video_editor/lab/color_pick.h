// Color Pick — eyedropper helpers for the Object Lab / inspector:
// region averaging over packed BGRA8 frames, a small color-distance
// metric, and shared hex<->BGRA conversion so pickers, mattes and
// title styles speak one grammar.
//
// The hex grammar deliberately matches generated_source.cc's matte
// parser (ParseMatteColor): optional '#', 6 or 8 case-insensitive hex
// digits, RRGGBB[AA] order in the string, {b,g,r,a} out. This header
// is the shared home for that conversion; ParseMatteColor predates it
// and stays untouched.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "video/frame.h"

namespace XER::VideoEditor::Lab {

// Area-average eyedropper: mean BGRA over the (2*radius+1)^2 window
// centered at (x,y), clamped to frame bounds; radius <= 0 samples the
// single pixel. Returns {b,g,r,a} rounded to nearest; a non-BGRA8
// frame, or a window that touches nothing, yields {0,0,0,0}.
std::array<std::uint8_t, 4> SampleRegionAvg(const video::Frame& frame,
                                            int x, int y, int radius);

// Euclidean distance between two BGRA colors over all four channels —
// 0 for identical, ~510 for black vs opaque white.
float ColorDistance(const std::array<std::uint8_t, 4>& a,
                    const std::array<std::uint8_t, 4>& b);

// "#RRGGBB" / "#RRGGBBAA" -> {b,g,r,a}. The '#' is optional, hex is
// case-insensitive; a missing alpha byte yields 255. Anything
// malformed (empty, wrong digit count, non-hex chars) yields opaque
// black {0,0,0,255} — same lenient contract as ParseMatteColor.
std::array<std::uint8_t, 4> HexToBgra(const std::string& text);

// {b,g,r,a} -> "#RRGGBB" (alpha=false) or "#RRGGBBAA" (alpha=true,
// the default). Always lowercase-free canonical form: '#', uppercase
// hex — so BgraToHex(HexToBgra(s)) normalizes user input.
std::string BgraToHex(const std::array<std::uint8_t, 4>& bgra,
                      bool include_alpha = true);

}  // namespace XER::VideoEditor::Lab
