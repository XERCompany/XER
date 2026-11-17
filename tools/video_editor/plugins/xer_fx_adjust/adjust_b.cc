#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fx {
namespace {

inline float param(const float* p, int32_t n, int32_t i, float def) {
    return (p && i < n) ? p[i] : def;
}

inline uint8_t clamp8(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f));
}

inline bool valid(const XER_VideoFrame* f) {
    return f && f->data && f->width > 0 && f->height > 0 && f->stride >= f->width * 4;
}

}  // namespace

int temperature(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float d = std::clamp(param(p, n, 0, 0.f), -100.f, 100.f) * 0.6f;
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + y * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            px[0] = clamp8(px[0] - d);
            px[2] = clamp8(px[2] + d);
        }
    }
    return 0;
}

int tint(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float d = std::clamp(param(p, n, 0, 0.f), -100.f, 100.f) * 0.5f;
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + y * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            px[0] = clamp8(px[0] + d);
            px[1] = clamp8(px[1] - d);
            px[2] = clamp8(px[2] + d);
        }
    }
    return 0;
}

int vibrance(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float amt = std::clamp(param(p, n, 0, 0.f), -100.f, 100.f) / 100.f;
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + y * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const float b = px[0], g = px[1], r = px[2];
            const float s = std::max({r, g, b}) - std::min({r, g, b});
            const float delta = (1.f - s / 255.f) * amt;
            const float luma = 0.114f * b + 0.587f * g + 0.299f * r;
            px[0] = clamp8(b + (b - luma) * delta);
            px[1] = clamp8(g + (g - luma) * delta);
            px[2] = clamp8(r + (r - luma) * delta);
        }
    }
    return 0;
}

int lift_gamma_gain(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float lift = std::clamp(param(p, n, 0, 0.f), -1.f, 1.f);
    const float gamma = std::clamp(param(p, n, 1, 1.f), 0.2f, 4.f);
    const float gain = std::clamp(param(p, n, 2, 1.f), 0.f, 4.f);
    const float inv = 1.f / gamma;
    uint8_t lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = clamp8(std::pow(std::max(i / 255.f + lift, 0.f), inv) * gain * 255.f);
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + y * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            px[0] = lut[px[0]];
            px[1] = lut[px[1]];
            px[2] = lut[px[2]];
        }
    }
    return 0;
}

}  // namespace fx
