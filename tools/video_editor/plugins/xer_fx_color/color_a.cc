#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fx {
namespace {

inline float param(const float* p, int32_t n, int32_t i, float d) {
    return (p && i < n) ? p[i] : d;
}

inline uint8_t clamp8(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f) + 0.5f);
}

// hue degrees -> pure RGB (s = v = 1), components in [0..1]
inline void hue_to_rgb(float h, float& r, float& g, float& b) {
    h = std::fmod(h, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float x = 1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f);
    if      (h < 60.0f)  { r = 1.0f; g = x;    b = 0.0f; }
    else if (h < 120.0f) { r = x;    g = 1.0f; b = 0.0f; }
    else if (h < 180.0f) { r = 0.0f; g = 1.0f; b = x;    }
    else if (h < 240.0f) { r = 0.0f; g = x;    b = 1.0f; }
    else if (h < 300.0f) { r = x;    g = 0.0f; b = 1.0f; }
    else                 { r = 1.0f; g = 0.0f; b = x;    }
}

template <typename F>
void for_each_pixel(XER_VideoFrame* f, F&& fn) {
    if (!f || !f->data) return;
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<int64_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            uint8_t* px = row + x * 4;
            fn(px[2], px[1], px[0]);  // r, g, b; alpha untouched
        }
    }
}

}  // namespace

int hue_rotate(XER_VideoFrame* f, const float* params, int32_t n) {
    const float a = param(params, n, 0, 0.0f) * (3.14159265358979323846f / 180.0f);
    const float c = std::cos(a), s = std::sin(a);
    const float m[9] = {
        0.299f + 0.701f * c + 0.168f * s, 0.587f - 0.587f * c + 0.330f * s, 0.114f - 0.114f * c - 0.497f * s,
        0.299f - 0.299f * c - 0.328f * s, 0.587f + 0.413f * c + 0.035f * s, 0.114f - 0.114f * c + 0.292f * s,
        0.299f - 0.300f * c + 1.250f * s, 0.587f - 0.588f * c - 1.050f * s, 0.114f + 0.886f * c - 0.203f * s,
    };
    for_each_pixel(f, [&](uint8_t& r, uint8_t& g, uint8_t& b) {
        const float fr = r, fg = g, fb = b;
        r = clamp8(m[0] * fr + m[1] * fg + m[2] * fb);
        g = clamp8(m[3] * fr + m[4] * fg + m[5] * fb);
        b = clamp8(m[6] * fr + m[7] * fg + m[8] * fb);
    });
    return 0;
}

int sepia_tone(XER_VideoFrame* f, const float* params, int32_t n) {
    const float t = std::clamp(param(params, n, 0, 1.0f), 0.0f, 1.0f);
    for_each_pixel(f, [&](uint8_t& r, uint8_t& g, uint8_t& b) {
        const float fr = r, fg = g, fb = b;
        const float sr = 0.393f * fr + 0.769f * fg + 0.189f * fb;
        const float sg = 0.349f * fr + 0.686f * fg + 0.168f * fb;
        const float sb = 0.272f * fr + 0.534f * fg + 0.131f * fb;
        r = clamp8(fr + (sr - fr) * t);
        g = clamp8(fg + (sg - fg) * t);
        b = clamp8(fb + (sb - fb) * t);
    });
    return 0;
}

int duotone(XER_VideoFrame* f, const float* params, int32_t n) {
    float sr, sg, sb, hr, hg, hb;
    hue_to_rgb(param(params, n, 0, 220.0f), sr, sg, sb);
    hue_to_rgb(param(params, n, 1, 40.0f), hr, hg, hb);
    for_each_pixel(f, [&](uint8_t& r, uint8_t& g, uint8_t& b) {
        const float l = (0.299f * r + 0.587f * g + 0.114f * b) * (1.0f / 255.0f);
        r = clamp8((sr + (hr - sr) * l) * 255.0f);
        g = clamp8((sg + (hg - sg) * l) * 255.0f);
        b = clamp8((sb + (hb - sb) * l) * 255.0f);
    });
    return 0;
}

int channel_mixer(XER_VideoFrame* f, const float* params, int32_t n) {
    const float gr = std::clamp(param(params, n, 0, 1.0f), 0.0f, 2.0f);
    const float gg = std::clamp(param(params, n, 1, 1.0f), 0.0f, 2.0f);
    const float gb = std::clamp(param(params, n, 2, 1.0f), 0.0f, 2.0f);
    for_each_pixel(f, [&](uint8_t& r, uint8_t& g, uint8_t& b) {
        r = clamp8(r * gr);
        g = clamp8(g * gg);
        b = clamp8(b * gb);
    });
    return 0;
}

}  // namespace fx
