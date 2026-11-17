// Impl file A — directional, radial and zoom blur effects.
#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr int32_t kSamples = 16;

float param(const float* p, int32_t n, int32_t i, float d) {
    return (p && i < n) ? p[i] : d;
}

bool valid(const XER_VideoFrame* f) {
    return f && f->data && f->width > 0 && f->height > 0 &&
           f->stride >= f->width * 4;
}

// Nearest-neighbour fetch with edge clamping.
const uint8_t* at(const uint8_t* s, int32_t w, int32_t h, int32_t st, float x,
                  float y) {
    const int32_t ix = std::clamp(static_cast<int32_t>(std::lround(x)), 0, w - 1);
    const int32_t iy = std::clamp(static_cast<int32_t>(std::lround(y)), 0, h - 1);
    return s + static_cast<size_t>(iy) * st + static_cast<size_t>(ix) * 4;
}

void store(uint8_t* d, float b, float g, float r, float a, float inv) {
    d[0] = static_cast<uint8_t>(b * inv);
    d[1] = static_cast<uint8_t>(g * inv);
    d[2] = static_cast<uint8_t>(r * inv);
    d[3] = static_cast<uint8_t>(a * inv);
}

}  // namespace

int directional_blur(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const int32_t r = std::clamp(
        static_cast<int32_t>(std::lround(param(p, n, 0, 12.f))), 1, 64);
    const float ang = param(p, n, 1, 0.f) * kPi / 180.f;
    const float dx = std::cos(ang), dy = std::sin(ang);
    const int32_t w = f->width, h = f->height, st = f->stride;
    const std::vector<uint8_t> src(f->data,
                                   f->data + static_cast<size_t>(h) * st);
    const float inv = 1.f / static_cast<float>(2 * r + 1);
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int32_t x = 0; x < w; ++x) {
            float b = 0, g = 0, rr = 0, a = 0;
            for (int32_t k = -r; k <= r; ++k) {
                const uint8_t* s =
                    at(src.data(), w, h, st, x + dx * k, y + dy * k);
                b += s[0];
                g += s[1];
                rr += s[2];
                a += s[3];
            }
            store(row + static_cast<size_t>(x) * 4, b, g, rr, a, inv);
        }
    }
    return 0;
}

int radial_blur(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float s = std::clamp(param(p, n, 0, 0.3f), 0.f, 1.f);
    const float cx =
        std::clamp(param(p, n, 1, 0.5f), 0.f, 1.f) * (f->width - 1);
    const float cy =
        std::clamp(param(p, n, 2, 0.5f), 0.f, 1.f) * (f->height - 1);
    const int32_t w = f->width, h = f->height, st = f->stride;
    const std::vector<uint8_t> src(f->data,
                                   f->data + static_cast<size_t>(h) * st);
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int32_t x = 0; x < w; ++x) {
            float b = 0, g = 0, rr = 0, a = 0, wsum = 0;
            for (int32_t i = 0; i < kSamples; ++i) {
                const float t = static_cast<float>(i) / (kSamples - 1.f);
                const float wgt = static_cast<float>(kSamples - i);
                const uint8_t* q =
                    at(src.data(), w, h, st, x + (cx - x) * t * s,
                       y + (cy - y) * t * s);
                b += q[0] * wgt;
                g += q[1] * wgt;
                rr += q[2] * wgt;
                a += q[3] * wgt;
                wsum += wgt;
            }
            store(row + static_cast<size_t>(x) * 4, b, g, rr, a, 1.f / wsum);
        }
    }
    return 0;
}

int zoom_blur(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float s = std::clamp(param(p, n, 0, 0.3f), 0.f, 1.f);
    const int32_t w = f->width, h = f->height, st = f->stride;
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const std::vector<uint8_t> src(f->data,
                                   f->data + static_cast<size_t>(h) * st);
    constexpr float inv = 1.f / static_cast<float>(kSamples);
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int32_t x = 0; x < w; ++x) {
            const float rx = x - cx, ry = y - cy;
            float b = 0, g = 0, rr = 0, a = 0;
            for (int32_t i = 0; i < kSamples; ++i) {
                const float t = static_cast<float>(i) / (kSamples - 1.f);
                const float d = 1.f - s * t;
                const uint8_t* q =
                    at(src.data(), w, h, st, cx + rx * d, cy + ry * d);
                b += q[0];
                g += q[1];
                rr += q[2];
                a += q[3];
            }
            store(row + static_cast<size_t>(x) * 4, b, g, rr, a, inv);
        }
    }
    return 0;
}

}  // namespace fx
