// libdemo/src/dsp.cc — see include/libdemo/dsp.h.
//
// XSJOBuildX compiles this TU in-process into an .xsjo module inside
// libdemo.xejslib; a consuming extension gets it linked as
// mobjective/lib_dsp.xsjo. Keep it freestanding: no libc/libm
// references — the JIT has no process-symbol fallback.
#include "libdemo/dsp.h"

namespace libdemo {

int32_t ClampI32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

int32_t ApplyGainQ15(int32_t sample, int32_t gain_q15) {
    const int64_t r =
        (static_cast<int64_t>(sample) * gain_q15) >> 15;
    return ClampI32(static_cast<int32_t>(r), INT32_MIN, INT32_MAX);
}

int32_t LerpQ15(int32_t a, int32_t b, int32_t t_q15) {
    return static_cast<int32_t>(
        a + ((static_cast<int64_t>(b - a) * t_q15) >> 15));
}

int32_t GainDbQ15(int32_t db) {
    // Per-dB factors in Q16: 10^(+1/20) ~= 1.122018 -> 73551,
    // 10^(-1/20) ~= 0.891251 -> 58421. Stepping whole dBs keeps the
    // error inside ~0.1% across the usable range — plenty for a
    // demo helper, and no libm is ever referenced.
    constexpr int64_t kUpQ16 = 73551;
    constexpr int64_t kDownQ16 = 58421;
    int64_t acc = 32768;   // 1.0 in Q15
    int32_t steps = db;
    if (steps > 48) steps = 48;
    if (steps < -90) steps = -90;
    while (steps > 0) {
        acc = (acc * kUpQ16) >> 16;
        --steps;
    }
    while (steps < 0) {
        acc = (acc * kDownQ16) >> 16;
        ++steps;
    }
    return ClampI32(static_cast<int32_t>(acc), 0, INT32_MAX);
}

const char* Version() {
    return "libdemo/1.0";
}

}  // namespace libdemo
