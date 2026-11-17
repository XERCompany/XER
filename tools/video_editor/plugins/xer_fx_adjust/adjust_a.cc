// Impl file — exposure / gamma / levels / auto_contrast for BGRA8 frames.
#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fx {
namespace {

bool valid(const XER_VideoFrame* f, const float* p, int32_t n) {
    return f && f->data && f->width > 0 && f->height > 0 &&
           f->stride >= f->width * 4 && (n <= 0 || p);
}

void map_rows(XER_VideoFrame* f, const uint8_t lut[3][256]) {
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            px[0] = lut[0][px[0]];
            px[1] = lut[1][px[1]];
            px[2] = lut[2][px[2]];
        }
    }
}

void fill_lut(uint8_t lut[256], auto fn) {
    for (int i = 0; i < 256; ++i)
        lut[i] = static_cast<uint8_t>(
            std::clamp(std::lround(fn(static_cast<float>(i))), 0l, 255l));
}

float param(const float* p, int32_t n, int32_t i, float dflt) {
    return n > i ? p[i] : dflt;
}

}  // namespace

int exposure(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f, p, n)) return 1;
    const float scale = std::exp2(std::clamp(param(p, n, 0, 0.f), -4.f, 4.f));
    uint8_t lut[3][256];
    fill_lut(lut[0], [scale](float v) { return v * scale; });
    std::copy_n(lut[0], 256, lut[1]);
    std::copy_n(lut[0], 256, lut[2]);
    map_rows(f, lut);
    return 0;
}

int gamma_corr(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f, p, n)) return 1;
    const float inv = 1.f / std::clamp(param(p, n, 0, 1.f), 0.2f, 4.f);
    uint8_t lut[3][256];
    fill_lut(lut[0], [inv](float v) { return 255.f * std::pow(v / 255.f, inv); });
    std::copy_n(lut[0], 256, lut[1]);
    std::copy_n(lut[0], 256, lut[2]);
    map_rows(f, lut);
    return 0;
}

int levels(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f, p, n)) return 1;
    const float black = std::clamp(param(p, n, 0, 0.f), 0.f, 128.f);
    const float white = std::clamp(param(p, n, 1, 255.f), 128.f, 255.f);
    const float inv = 1.f / std::clamp(param(p, n, 2, 1.f), 0.2f, 4.f);
    const float span = std::max(white - black, 1.f);
    uint8_t lut[3][256];
    fill_lut(lut[0], [=](float v) {
        const float t = std::clamp((v - black) / span, 0.f, 1.f);
        return 255.f * std::pow(t, inv);
    });
    std::copy_n(lut[0], 256, lut[1]);
    std::copy_n(lut[0], 256, lut[2]);
    map_rows(f, lut);
    return 0;
}

int auto_contrast(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f, p, n)) return 1;
    const float clip_pct = std::clamp(param(p, n, 0, 0.5f), 0.f, 10.f);

    uint32_t hist[3][256]{};
    for (int32_t y = 0; y < f->height; ++y) {
        const uint8_t* px = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            ++hist[0][px[0]];
            ++hist[1][px[1]];
            ++hist[2][px[2]];
        }
    }

    const int64_t clip = static_cast<int64_t>(
        static_cast<double>(f->width) * f->height * clip_pct / 100.0);
    uint8_t lut[3][256];
    for (int c = 0; c < 3; ++c) {
        int lo = 0, hi = 255;
        int64_t acc = 0;
        while (lo < 255 && acc + hist[c][lo] <= clip) acc += hist[c][lo++];
        acc = 0;
        while (hi > 0 && acc + hist[c][hi] <= clip) acc += hist[c][hi--];
        if (hi <= lo) {
            for (int i = 0; i < 256; ++i) lut[c][i] = static_cast<uint8_t>(i);
        } else {
            const float s = 255.f / (hi - lo);
            fill_lut(lut[c], [=](float v) { return (v - lo) * s; });
        }
    }
    map_rows(f, lut);
    return 0;
}

}  // namespace fx
