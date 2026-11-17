// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int wave_h(XER_VideoFrame*, const float*, int32_t);      // p0 amplitude px 0..64, p1 wavelength px 8..512
int wave_v(XER_VideoFrame*, const float*, int32_t);      // p0 amplitude px 0..64, p1 wavelength px 8..512
int ripple(XER_VideoFrame*, const float*, int32_t);       // p0 amplitude px 0..32, p1 wavelength px 8..256
int mirror_quad(XER_VideoFrame*, const float*, int32_t);  // p0 mode 0=left 1=right 2=top 3=bottom
int tile(XER_VideoFrame*, const float*, int32_t);         // p0 tiles per side 2..8
int twirl(XER_VideoFrame*, const float*, int32_t);        // p0 angle deg -360..360

}  // namespace fx
