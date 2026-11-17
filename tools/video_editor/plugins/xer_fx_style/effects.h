// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int solarize(XER_VideoFrame*, const float*, int32_t);    // p0 threshold 0..255
int threshold_bw(XER_VideoFrame*, const float*, int32_t); // p0 level 0..255
int scanlines(XER_VideoFrame*, const float*, int32_t);    // p0 spacing px 2..16, p1 strength 0..1
int neon_edges(XER_VideoFrame*, const float*, int32_t);   // p0 threshold 0..255, p1 hue deg 0..360
int halftone(XER_VideoFrame*, const float*, int32_t);     // p0 cell px 4..32
int emboss_color(XER_VideoFrame*, const float*, int32_t); // p0 angle deg, p1 strength 0..2
int edge_glow(XER_VideoFrame*, const float*, int32_t);    // p0 threshold 0..255, p1 intensity 0..4
int tone_split(XER_VideoFrame*, const float*, int32_t);   // p0 shadow hue deg, p1 highlight hue deg

}  // namespace fx
