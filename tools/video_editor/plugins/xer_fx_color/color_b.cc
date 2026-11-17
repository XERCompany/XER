// Impl file — invert_lum, color_isolate, rgb_swap, saturation_boost.
#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

float param(const float* p, int32_t n, int i, float def) {
    return (p && i < n) ? p[i] : def;
}

inline float luma(float b, float g, float r) {
    return 0.114f * b + 0.587f * g + 0.299f * r;
}

inline uint8_t c8(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f) + 0.5f);
}

// HSV hue in degrees [0,360) from 0..255 channels.
float hue_of(float b, float g, float r) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float d = mx - mn;
    if (d <= 0.f) return 0.f;
    float h;
    if (mx == r)      h = 60.f * std::fmod((g - b) / d, 6.f);
    else if (mx == g) h = 60.f * ((b - r) / d + 2.f);
    else              h = 60.f * ((r - g) / d + 4.f);
    if (h < 0.f) h += 360.f;
    return h;
}

}  // namespace

namespace fx {

int invert_lum(XER_VideoFrame* f, const float* params, int32_t n) {
    const float amt = std::clamp(param(params, n, 0, 1.f), 0.f, 1.f);
    uint8_t* row = f->data;
    for (int32_t y = 0; y < f->height; ++y, row += f->stride) {
        uint8_t* px = row;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const float b = px[0], g = px[1], r = px[2];
            const float shift = amt * (255.f - 2.f * luma(b, g, r));
            px[0] = c8(b + shift);
            px[1] = c8(g + shift);
            px[2] = c8(r + shift);
        }
    }
    return 0;
}

int color_isolate(XER_VideoFrame* f, const float* params, int32_t n) {
    const float target = param(params, n, 0, 0.f);
    const float tol = std::clamp(param(params, n, 1, 20.f), 1.f, 90.f);
    const float feather = std::min(20.f, tol * 0.5f);
    uint8_t* row = f->data;
    for (int32_t y = 0; y < f->height; ++y, row += f->stride) {
        uint8_t* px = row;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const float b = px[0], g = px[1], r = px[2];
            float d = std::fabs(hue_of(b, g, r) - target);
            if (d > 180.f) d = 360.f - d;
            const float k = std::clamp((tol + feather - d) / feather, 0.f, 1.f);
            if (k >= 1.f) continue;
            const float ly = luma(b, g, r);
            px[0] = c8(ly + (b - ly) * k);
            px[1] = c8(ly + (g - ly) * k);
            px[2] = c8(ly + (r - ly) * k);
        }
    }
    return 0;
}

int rgb_swap(XER_VideoFrame* f, const float* params, int32_t n) {
    static constexpr uint8_t kPerm[6][3] = {
        {0, 1, 2},  // BGR
        {0, 2, 1},  // BRG
        {1, 0, 2},  // GBR
        {1, 2, 0},  // GRB
        {2, 0, 1},  // RBG
        {2, 1, 0},  // RGB
    };
    const int ord =
        std::clamp(static_cast<int>(param(params, n, 0, 0.f) + 0.5f), 0, 5);
    if (ord == 0) return 0;
    const uint8_t* pm = kPerm[ord];
    uint8_t* row = f->data;
    for (int32_t y = 0; y < f->height; ++y, row += f->stride) {
        uint8_t* px = row;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const uint8_t c0 = px[0], c1 = px[1], c2 = px[2];
            const uint8_t src[3] = {c0, c1, c2};
            px[0] = src[pm[0]];
            px[1] = src[pm[1]];
            px[2] = src[pm[2]];
        }
    }
    return 0;
}

int saturation_boost(XER_VideoFrame* f, const float* params, int32_t n) {
    const float amt = std::clamp(param(params, n, 0, 30.f), -100.f, 100.f);
    const float scale = 1.f + amt / 100.f;
    uint8_t* row = f->data;
    for (int32_t y = 0; y < f->height; ++y, row += f->stride) {
        uint8_t* px = row;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const float b = px[0], g = px[1], r = px[2];
            const float ly = luma(b, g, r);
            px[0] = c8(ly + (b - ly) * scale);
            px[1] = c8(ly + (g - ly) * scale);
            px[2] = c8(ly + (r - ly) * scale);
        }
    }
    return 0;
}

}  // namespace fx
