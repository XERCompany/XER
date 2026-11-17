// Declares this plugin's effect implementations. Impl files assign
// bodies to these prototypes; exports.cc binds them into the ABI
// table. Video fns: BGRA8 in-place, return 0 on success.
#pragma once

#include "../../sdk/xer_plugin.h"

namespace fx {
int echo(XER_AudioBuffer*, const float*, int32_t);        // p0 delay ms 1..2000, p1 feedback 0..0.95, p2 mix 0..1
int tremolo(XER_AudioBuffer*, const float*, int32_t);     // p0 rate Hz 0.1..20, p1 depth 0..1
int autopan(XER_AudioBuffer*, const float*, int32_t);     // p0 rate Hz 0.1..20, p1 depth 0..1
int bitcrusher(XER_AudioBuffer*, const float*, int32_t);  // p0 bits 1..16, p1 downsample 1..64
int onepole_lp(XER_AudioBuffer*, const float*, int32_t);  // p0 cutoff Hz 100..18000
int soft_clip(XER_AudioBuffer*, const float*, int32_t);   // p0 drive 1..20

}  // namespace fx
