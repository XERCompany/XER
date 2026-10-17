#include "wrapper/skia/skia_engine_bridge_m44.h"
#include <cmath>

#define PI 3.14159265358979323846f

#if ENGINE_HAS_SKIA_BRIDGE
#include "include/core/SkM44.h"
#endif

namespace engine::bridge::skia {

#if ENGINE_HAS_SKIA_BRIDGE

static Matrix4x4 SkToStd(const SkM44& m) {
    Matrix4x4 res;
    m.getColMajor(res.data());
    return res;
}

static SkM44 StdToSk(const Matrix4x4& m) {
    return SkM44::ColMajor(m.data());
}

Matrix4x4 M44Identity() {
    return SkToStd(SkM44());
}

Matrix4x4 M44Translate(float x, float y, float z) {
    return SkToStd(SkM44::Translate(x, y, z));
}

Matrix4x4 M44Scale(float x, float y, float z) {
    return SkToStd(SkM44::Scale(x, y, z));
}

Matrix4x4 M44RotateX(float degrees) {
    float rad = degrees * (float)M_PI / 180.0f;
    return SkToStd(SkM44::Rotate({1, 0, 0}, rad));
}

Matrix4x4 M44RotateY(float degrees) {
    float rad = degrees * (float)M_PI / 180.0f;
    return SkToStd(SkM44::Rotate({0, 1, 0}, rad));
}

Matrix4x4 M44RotateZ(float degrees) {
    float rad = degrees * (float)M_PI / 180.0f;
    return SkToStd(SkM44::Rotate({0, 0, 1}, rad));
}

Matrix4x4 M44Multiply(const Matrix4x4& a, const Matrix4x4& b) {
    SkM44 ma = StdToSk(a);
    SkM44 mb = StdToSk(b);
    return SkToStd(ma * mb);
}

#else

// Pure C++ 4x4 column-major matrix fallbacks (no Skia dependency).
// Stored as float[16] in column-major order:
//   m[col*4 + row]

static Matrix4x4 MakeDiag(float s) {
    Matrix4x4 m{};
    m[0] = m[5] = m[10] = m[15] = s;
    return m;
}

Matrix4x4 M44Identity() {
    return MakeDiag(1.0f);
}

Matrix4x4 M44Translate(float x, float y, float z) {
    Matrix4x4 m = MakeDiag(1.0f);
    m[12] = x;   // col 3, row 0
    m[13] = y;   // col 3, row 1
    m[14] = z;   // col 3, row 2
    return m;
}

Matrix4x4 M44Scale(float x, float y, float z) {
    Matrix4x4 m{};
    m[0]  = x;
    m[5]  = y;
    m[10] = z;
    m[15] = 1.0f;
    return m;
}

Matrix4x4 M44RotateX(float degrees) {
    const float rad = degrees * static_cast<float>(PI) / 180.0f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    Matrix4x4 m = MakeDiag(1.0f);
    // column-major: col1,row1 = c  col1,row2 = s  col2,row1 = -s  col2,row2 = c
    m[5]  =  c;   // col 1, row 1
    m[6]  =  s;   // col 1, row 2
    m[9]  = -s;   // col 2, row 1
    m[10] =  c;   // col 2, row 2
    return m;
}

Matrix4x4 M44RotateY(float degrees) {
    const float rad = degrees * static_cast<float>(PI) / 180.0f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    Matrix4x4 m = MakeDiag(1.0f);
    m[0]  =  c;   // col 0, row 0
    m[2]  = -s;   // col 0, row 2
    m[8]  =  s;   // col 2, row 0
    m[10] =  c;   // col 2, row 2
    return m;
}

Matrix4x4 M44RotateZ(float degrees) {
    const float rad = degrees * static_cast<float>(PI) / 180.0f;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    Matrix4x4 m = MakeDiag(1.0f);
    m[0] =  c;   // col 0, row 0
    m[1] =  s;   // col 0, row 1
    m[4] = -s;   // col 1, row 0
    m[5] =  c;   // col 1, row 1
    return m;
}

Matrix4x4 M44Multiply(const Matrix4x4& a, const Matrix4x4& b) {
    Matrix4x4 res{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a[k * 4 + row] * b[col * 4 + k];
            }
            res[col * 4 + row] = sum;
        }
    }
    return res;
}

#endif

}
