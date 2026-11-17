// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int exposure(XER_VideoFrame*, const float*, int32_t);   // p0 stops -4..4
int gamma_corr(XER_VideoFrame*, const float*, int32_t);   // p0 gamma 0.2..4
int levels(XER_VideoFrame*, const float*, int32_t);       // p0 black 0..128, p1 white 128..255, p2 mid gamma 0.2..4
int auto_contrast(XER_VideoFrame*, const float*, int32_t);// p0 clip% 0..10
int temperature(XER_VideoFrame*, const float*, int32_t);  // p0 -100..100
int tint(XER_VideoFrame*, const float*, int32_t);         // p0 green-magenta -100..100
int vibrance(XER_VideoFrame*, const float*, int32_t);     // p0 -100..100
int lift_gamma_gain(XER_VideoFrame*, const float*, int32_t); // p0 lift -1..1, p1 gamma 0.2..4, p2 gain 0..4

}  // namespace fx
