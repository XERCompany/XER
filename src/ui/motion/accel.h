#ifndef XER_UI_MOTION_ACCEL_H
#define XER_UI_MOTION_ACCEL_H

// SIMD-accelerated batch math for the motion layer. Per-widget tweens are
// scalar and cheap; the payoff is batch kernels — evaluating the same
// transcendental function over hundreds of values per frame (waveform bars,
// spectrum bins, particle fields).
//
// When XER_HAVE_MKL is defined the kernels dispatch to Intel oneMKL's
// Vector Math Library (VML): SIMD + multi-architecture dispatch (AVX2 /
// AVX-512 where available). Without MKL they fall back to scalar libm, so
// callers never need #ifdefs.

#include <cstddef>

namespace XER::UI::Motion::Accel {

// True when the Intel MKL VML backend is linked (batch kernels then run
// vectorized instead of scalar libm).
bool HasVectorMath();

// out[i] = sin(in[i]) / cos(in[i]) for i in [0, n).
// in and out must not alias. n == 0 is a no-op.
void Sin(const float* in, float* out, std::size_t n);
void Cos(const float* in, float* out, std::size_t n);

// out[i] = sin(pi * in[i]) / cos(pi * in[i]) — one ULP cheaper argument
// range for periodic UI waves.
void SinPi(const float* in, float* out, std::size_t n);
void CosPi(const float* in, float* out, std::size_t n);

} // namespace XER::UI::Motion::Accel

#endif // XER_UI_MOTION_ACCEL_H
