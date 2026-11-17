// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int hue_rotate(XER_VideoFrame*, const float*, int32_t);   // p0 degrees -180..180
int sepia_tone(XER_VideoFrame*, const float*, int32_t);   // p0 amount 0..1
int duotone(XER_VideoFrame*, const float*, int32_t);      // p0 shadow hue deg 0..360, p1 light hue deg
int channel_mixer(XER_VideoFrame*, const float*, int32_t);// p0 r gain, p1 g gain, p2 b gain 0..2
int invert_lum(XER_VideoFrame*, const float*, int32_t);   // p0 amount 0..1
int color_isolate(XER_VideoFrame*, const float*, int32_t);// p0 target hue deg, p1 tolerance deg 1..90
int rgb_swap(XER_VideoFrame*, const float*, int32_t);     // p0 channel order 0..5
int saturation_boost(XER_VideoFrame*, const float*, int32_t); // p0 -100..100

}  // namespace fx
