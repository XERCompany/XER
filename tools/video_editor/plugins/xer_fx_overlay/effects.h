// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int film_grain(XER_VideoFrame*, const float*, int32_t); // p0 amount 0..100, p1 monochrome 0/1
int letterbox(XER_VideoFrame*, const float*, int32_t);    // p0 bar fraction 0..0.45
int vignette_soft(XER_VideoFrame*, const float*, int32_t);// p0 strength 0..1, p1 radius 0..1
int grid_lines(XER_VideoFrame*, const float*, int32_t);   // p0 cells 2..16, p1 alpha 0..1
int border_frame(XER_VideoFrame*, const float*, int32_t); // p0 thickness px 0..64
int corner_marks(XER_VideoFrame*, const float*, int32_t); // p0 length px 4..64

}  // namespace fx
