// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int directional_blur(XER_VideoFrame*, const float*, int32_t); // p0 radius px 1..64, p1 angle deg
int radial_blur(XER_VideoFrame*, const float*, int32_t);  // p0 strength 0..1, p1 cx 0..1, p2 cy 0..1
int zoom_blur(XER_VideoFrame*, const float*, int32_t);    // p0 strength 0..1
int fast_gaussian(XER_VideoFrame*, const float*, int32_t);// p0 radius px 1..32
int unsharp_mask(XER_VideoFrame*, const float*, int32_t); // p0 amount 0..4, p1 radius 1..16
int median3(XER_VideoFrame*, const float*, int32_t);      // p0 passes 1..4

}  // namespace fx
