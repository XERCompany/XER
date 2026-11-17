// Object Lab math acceleration (Phase 2). Runtime-probed BLAS backend:
// the lab never links an MKL/OpenBLAS import library, so the core lib
// works everywhere. Probe order on Windows:
//
//   1. Intel oneMKL — mkl_rt*.dll located via known oneAPI install dirs,
//      %ONEAPI_ROOT%, then PATH (LoadLibraryW + GetProcAddress).
//   2. OpenBLAS — engaged only when a cblas.h header was visible at
//      build time (__has_include); the DLL itself is still resolved at
//      runtime via the MSYS2 clang64 bin dir and PATH.
//   3. Scalar C++ loops — always available; -O3 auto-vectorizes them.
//
// Init can never fail: worst case ActiveBackend() reports Scalar.
#pragma once

namespace XER::VideoEditor::Lab {

enum class AccelBackend {
    Scalar = 0,
    MKL,
    OpenBLAS,
};

// Lazily probes on first call (thread-safe via static init).
AccelBackend ActiveBackend();

// Diagnostic string, e.g. "scalar", "mkl_rt (C:\\...\\mkl_rt.3.dll)".
// Owned by the backend state — do not free.
const char* BackendName();

// <a,b> dot product over n floats (incx=incy=1).
float Sdot(int n, const float* a, const float* b);

// Sum of squared differences  sum_i (a[i]-b[i])^2, clamped >= 0.
// Vector backends compute it as  <a,a> - 2<a,b> + <b,b>  (three sdot
// calls, no scratch buffer); the scalar backend runs one fused pass to
// avoid cancellation. This is the hot op of inpaint patch scoring.
float PatchSsd(int n, const float* a, const float* b);

}  // namespace XER::VideoEditor::Lab
