// libdemo/dsp.h — tiny freestanding DSP helpers shipped as a
// .xejslib library (built by XSJOBuildX, consumed by ztzmake --dep /
// [native] deps / libs/*.xejslib).
//
// The functions live in a C++ namespace on purpose: a consumer's
// undefined reference is the MANGLED name, and the library module
// exports it mangled — cross-module resolution through the JIT's
// meshed JITDylib link order (xsjo_jit.h::LinkModules) handles C++
// symbols exactly like extern "C" ones.
//
// Freestanding contract — identical to the extension-side rule in
// ve.core.h: no libc/libm calls. The JIT host resolves ve_* imports
// and module-to-module symbols only; an undefined CRT reference
// (memcpy, strlen, pow, ...) fails the link loudly.
#pragma once

#include <stdint.h>

namespace libdemo {

// Clamp v into [lo, hi]. lo > hi returns lo unchanged.
int32_t ClampI32(int32_t v, int32_t lo, int32_t hi);

// sample * gain_q15 >> 15 with 64-bit intermediate, clamped to
// int32. gain_q15 == 32768 is unity gain.
int32_t ApplyGainQ15(int32_t sample, int32_t gain_q15);

// a + ((b - a) * t_q15 >> 15). t_q15 = 0 -> a, 32768 -> b.
int32_t LerpQ15(int32_t a, int32_t b, int32_t t_q15);

// Integer-dB -> linear gain in Q15. db is truncated to whole
// decibels and stepped through the per-dB constant 10^(1/20)
// (Q16 73551 up / 58421 down), so -6 dB lands within ~0.1% of exact
// 0.501. Range is effectively -90..+48 dB (Q15 saturates at +18 dB).
int32_t GainDbQ15(int32_t db);

// "libdemo/<version>" — a string literal living in the library
// module's rodata; reading it also proves cross-module DATA symbol
// resolution, not just code.
const char* Version();

}  // namespace libdemo
