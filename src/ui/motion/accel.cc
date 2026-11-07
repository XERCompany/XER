#include "accel.h"

#include <cmath>

#ifdef XER_HAVE_MKL
#include <mkl_types.h>
#include <mkl_vml.h>
#endif

namespace XER::UI::Motion::Accel {
namespace {

#ifdef XER_HAVE_MKL
MKL_INT ToMklInt(std::size_t n) {
    // UI batches are at most a few thousand elements — far below INT32_MAX.
    return static_cast<MKL_INT>(n);
}
#endif

void SinScalar(const float* in, float* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = std::sin(in[i]);
    }
}

void CosScalar(const float* in, float* out, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = std::cos(in[i]);
    }
}

} // namespace

bool HasVectorMath() {
#ifdef XER_HAVE_MKL
    return true;
#else
    return false;
#endif
}

void Sin(const float* in, float* out, std::size_t n) {
    if (n == 0) {
        return;
    }
#ifdef XER_HAVE_MKL
    vsSin(ToMklInt(n), in, out);
#else
    SinScalar(in, out, n);
#endif
}

void Cos(const float* in, float* out, std::size_t n) {
    if (n == 0) {
        return;
    }
#ifdef XER_HAVE_MKL
    vsCos(ToMklInt(n), in, out);
#else
    CosScalar(in, out, n);
#endif
}

void SinPi(const float* in, float* out, std::size_t n) {
    if (n == 0) {
        return;
    }
#ifdef XER_HAVE_MKL
    vsSinpi(ToMklInt(n), in, out);
#else
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = std::sin(in[i] * 3.14159265358979323846f);
    }
#endif
}

void CosPi(const float* in, float* out, std::size_t n) {
    if (n == 0) {
        return;
    }
#ifdef XER_HAVE_MKL
    vsCospi(ToMklInt(n), in, out);
#else
    for (std::size_t i = 0; i < n; ++i) {
        out[i] = std::cos(in[i] * 3.14159265358979323846f);
    }
#endif
}

} // namespace XER::UI::Motion::Accel
