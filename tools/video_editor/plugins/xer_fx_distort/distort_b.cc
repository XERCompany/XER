#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

float param(const float* p, int32_t n, int32_t i, float def) {
    return (p && i < n) ? p[i] : def;
}

bool valid(const XER_VideoFrame* f) {
    return f && f->data && f->width > 0 && f->height > 0 && f->stride >= f->width * 4;
}

}  // namespace

int mirror_quad(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 0;
    const int w = f->width, h = f->height;
    const int mode = std::clamp(static_cast<int>(param(p, n, 0, 0.f)), 0, 3);
    const int hw = w / 2, hh = h / 2;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int x = 0; x < w; ++x) {
            int sx = x, sy = y;
            switch (mode) {
                case 0: if (x >= hw) sx = w - 1 - x; break;
                case 1: if (x < hw)  sx = w - 1 - x; break;
                case 2: if (y >= hh) sy = h - 1 - y; break;
                case 3: if (y < hh)  sy = h - 1 - y; break;
            }
            const uint8_t* s = f->data + static_cast<size_t>(sy) * f->stride + sx * 4;
            uint8_t* d = row + x * 4;
            if (s != d) std::copy_n(s, 4, d);
        }
    }
    return 0;
}

int tile(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 0;
    const int w = f->width, h = f->height;
    const int count = std::clamp(static_cast<int>(param(p, n, 0, 3.f)), 2, 8);
    const std::vector<uint8_t> src(f->data, f->data + static_cast<size_t>(h) * f->stride);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        const uint8_t* srow = src.data() + static_cast<size_t>((y * count) % h) * f->stride;
        for (int x = 0; x < w; ++x) {
            std::copy_n(srow + ((x * count) % w) * 4, 4, row + x * 4);
        }
    }
    return 0;
}

int twirl(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 0;
    const int w = f->width, h = f->height;
    const float deg = std::clamp(param(p, n, 0, 90.f), -360.f, 360.f);
    if (deg == 0.f) return 0;
    constexpr float kRad = 3.14159265358979323846f / 180.f;
    const float ang = deg * kRad;
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float maxD = std::sqrt(cx * cx + cy * cy);
    if (maxD <= 0.f) return 0;
    const std::vector<uint8_t> src(f->data, f->data + static_cast<size_t>(h) * f->stride);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int x = 0; x < w; ++x) {
            const float dx = x - cx, dy = y - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            float sx = static_cast<float>(x), sy = static_cast<float>(y);
            if (d < maxD) {
                const float t = ang * (1.f - d / maxD);
                const float c = std::cos(t), s = std::sin(t);
                sx = cx + dx * c - dy * s;
                sy = cy + dx * s + dy * c;
            }
            const int ix = std::clamp(static_cast<int>(std::lround(sx)), 0, w - 1);
            const int iy = std::clamp(static_cast<int>(std::lround(sy)), 0, h - 1);
            std::copy_n(src.data() + static_cast<size_t>(iy) * f->stride + ix * 4, 4, row + x * 4);
        }
    }
    return 0;
}

}  // namespace fx
