#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace fx {
namespace {

inline uint32_t xs32(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

// Deterministic per-pixel seed; warm-up scrambles nearby coordinates.
inline uint32_t pixel_seed(int32_t x, int32_t y) {
    uint32_t s = 0x9E3779B9u ^ static_cast<uint32_t>(x * 73856093) ^
                 static_cast<uint32_t>(y * 19349663);
    xs32(s);
    xs32(s);
    return s;
}

// Uniform noise in [-1, 1).
inline float noise01(uint32_t& s) {
    return static_cast<float>(xs32(s) >> 9) * (1.0f / 4194304.0f) - 1.0f;
}

inline float smoothstep(float e0, float e1, float x) {
    const float d = e1 - e0;
    const float t = d != 0.0f ? std::clamp((x - e0) / d, 0.0f, 1.0f)
                              : (x < e0 ? 0.0f : 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

inline bool valid(const XER_VideoFrame* f) {
    return f && f->data && f->width > 0 && f->height > 0 && f->stride >= f->width * 4;
}

}  // namespace

int film_grain(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float amount = n > 0 ? p[0] : 20.0f;
    const bool mono = (n > 1 ? p[1] : 1.0f) >= 0.5f;
    const float scale = std::clamp(amount, 0.0f, 100.0f) * 0.5f;

    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            uint8_t* px = row + x * 4;
            uint32_t s = pixel_seed(x, y);
            if (mono) {
                const float nz = noise01(s) * scale;
                for (int c = 0; c < 3; ++c)
                    px[c] = static_cast<uint8_t>(
                        std::clamp(static_cast<float>(px[c]) + nz, 0.0f, 255.0f));
            } else {
                for (int c = 0; c < 3; ++c)
                    px[c] = static_cast<uint8_t>(std::clamp(
                        static_cast<float>(px[c]) + noise01(s) * scale, 0.0f, 255.0f));
            }
        }
    }
    return 0;
}

int letterbox(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float frac = std::clamp(n > 0 ? p[0] : 0.12f, 0.0f, 0.45f);
    const int32_t bar = static_cast<int32_t>(std::lround(frac * f->height));

    for (int32_t y = 0; y < f->height; ++y) {
        if (y >= bar && y < f->height - bar) continue;
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            uint8_t* px = row + x * 4;
            px[0] = px[1] = px[2] = 0;
            px[3] = 255;
        }
    }
    return 0;
}

int vignette_soft(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float strength = std::clamp(n > 0 ? p[0] : 0.5f, 0.0f, 1.0f);
    const float radius = std::clamp(n > 1 ? p[1] : 0.7f, 0.0f, 1.0f);

    const float cx = f->width * 0.5f;
    const float cy = f->height * 0.5f;
    const float max_d = std::sqrt(cx * cx + cy * cy);
    if (max_d <= 0.0f) return 0;

    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        const float dy = (y + 0.5f) - cy;
        for (int32_t x = 0; x < f->width; ++x) {
            const float dx = (x + 0.5f) - cx;
            const float nd = std::sqrt(dx * dx + dy * dy) / max_d;
            const float factor = 1.0f - strength * smoothstep(radius, 1.0f, nd);
            uint8_t* px = row + x * 4;
            for (int c = 0; c < 3; ++c)
                px[c] = static_cast<uint8_t>(
                    std::clamp(static_cast<float>(px[c]) * factor, 0.0f, 255.0f));
        }
    }
    return 0;
}

}  // namespace fx
