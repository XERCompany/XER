#pragma once
#include <array>

namespace XER::Bridge::Skia {
    using Matrix4x4 = std::array<float, 16>;

    Matrix4x4 M44Identity();
    Matrix4x4 M44Translate(float x, float y, float z);
    Matrix4x4 M44Scale(float x, float y, float z);
    Matrix4x4 M44RotateX(float degrees);
    Matrix4x4 M44RotateY(float degrees);
    Matrix4x4 M44RotateZ(float degrees);
    Matrix4x4 M44Multiply(const Matrix4x4& a, const Matrix4x4& b);
}
