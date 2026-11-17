#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

constexpr float kTwoPi = 6.283185307179586f;

float param(const float* p, int32_t n, int32_t i, float lo, float hi, float def) {
    return std::clamp(p && i < n ? p[i] : def, lo, hi);
}

std::vector<uint8_t> snapshot(const XER_VideoFrame* f) {
    return {f->data, f->data + static_cast<size_t>(f->stride) * f->height};
}

void blit_px(uint8_t* dst, const uint8_t* src_row, int32_t sx) {
    std::copy_n(src_row + static_cast<size_t>(sx) * 4, 4, dst);
}

}  // namespace

int wave_h(XER_VideoFrame* f, const float* params, int32_t n) {
    const float amp = param(params, n, 0, 0.f, 64.f, 12.f);
    const float wl = param(params, n, 1, 8.f, 512.f, 120.f);
    const int32_t w = f->width, h = f->height;
    const std::vector<uint8_t> src = snapshot(f);
    for (int32_t y = 0; y < h; ++y) {
        const int32_t shift =
            static_cast<int32_t>(std::lround(amp * std::sin(kTwoPi * y / wl)));
        uint8_t* dst = f->data + static_cast<size_t>(y) * f->stride;
        const uint8_t* srow = src.data() + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x)
            blit_px(dst + static_cast<size_t>(x) * 4, srow,
                    std::clamp(x - shift, 0, w - 1));
    }
    return 0;
}

int wave_v(XER_VideoFrame* f, const float* params, int32_t n) {
    const float amp = param(params, n, 0, 0.f, 64.f, 12.f);
    const float wl = param(params, n, 1, 8.f, 512.f, 120.f);
    const int32_t w = f->width, h = f->height;
    const std::vector<uint8_t> src = snapshot(f);
    for (int32_t x = 0; x < w; ++x) {
        const int32_t shift =
            static_cast<int32_t>(std::lround(amp * std::sin(kTwoPi * x / wl)));
        for (int32_t y = 0; y < h; ++y) {
            const int32_t sy = std::clamp(y - shift, 0, h - 1);
            blit_px(f->data + static_cast<size_t>(y) * f->stride +
                        static_cast<size_t>(x) * 4,
                    src.data() + static_cast<size_t>(sy) * f->stride, x);
        }
    }
    return 0;
}

int ripple(XER_VideoFrame* f, const float* params, int32_t n) {
    const float amp = param(params, n, 0, 0.f, 32.f, 10.f);
    const float wl = param(params, n, 1, 8.f, 256.f, 60.f);
    const int32_t w = f->width, h = f->height;
    const std::vector<uint8_t> src = snapshot(f);
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float max_dist = std::max(std::hypot(cx, cy), 1e-6f);
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* dst = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x) {
            const float dx = x - cx, dy = y - cy;
            const float dist = std::hypot(dx, dy);
            int32_t sx = x, sy = y;
            if (dist > 1e-6f) {
                const float d = amp * std::sin(kTwoPi * dist / wl) *
                                std::max(0.f, 1.f - dist / max_dist);
                sx = static_cast<int32_t>(std::lround(x - dx / dist * d));
                sy = static_cast<int32_t>(std::lround(y - dy / dist * d));
            }
            blit_px(dst + static_cast<size_t>(x) * 4,
                    src.data() +
                        static_cast<size_t>(std::clamp(sy, 0, h - 1)) *
                            f->stride,
                    std::clamp(sx, 0, w - 1));
        }
    }
    return 0;
}

}  // namespace fx
