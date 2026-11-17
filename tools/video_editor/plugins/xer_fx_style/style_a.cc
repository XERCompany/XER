#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

float getp(const float* p, int32_t n, int32_t i, float dflt) {
    return (p != nullptr && i < n) ? p[i] : dflt;
}

float luma(const uint8_t* px) {  // Rec.601 on B,G,R
    return 0.114f * px[0] + 0.587f * px[1] + 0.299f * px[2];
}

void hue_to_bgr(float deg, uint8_t* out) {
    float h = std::fmod(deg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float x = h / 60.0f;
    const int i = static_cast<int>(x);
    const float f = x - static_cast<float>(i);
    float r, g, b;
    switch (i) {
        case 0:  r = 1.0f;     g = f;        b = 0.0f;     break;
        case 1:  r = 1.0f - f; g = 1.0f;     b = 0.0f;     break;
        case 2:  r = 0.0f;     g = 1.0f;     b = f;        break;
        case 3:  r = 0.0f;     g = 1.0f - f; b = 1.0f;     break;
        case 4:  r = f;        g = 0.0f;     b = 1.0f;     break;
        default: r = 1.0f;     g = 0.0f;     b = 1.0f - f; break;
    }
    out[0] = static_cast<uint8_t>(b * 255.0f + 0.5f);
    out[1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
    out[2] = static_cast<uint8_t>(r * 255.0f + 0.5f);
}

bool valid(const XER_VideoFrame* f) {
    return f != nullptr && f->data != nullptr && f->width > 0 &&
           f->height > 0 && f->stride >= f->width * 4;
}

}  // namespace

int solarize(XER_VideoFrame* f, const float* params, int32_t n) {
    if (!valid(f)) return 1;
    const int thr =
        static_cast<int>(std::clamp(getp(params, n, 0, 128.0f), 0.0f, 255.0f));
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, row += 4) {
            for (int c = 0; c < 3; ++c) {
                if (row[c] > thr) row[c] = static_cast<uint8_t>(255 - row[c]);
            }
        }
    }
    return 0;
}

int threshold_bw(XER_VideoFrame* f, const float* params, int32_t n) {
    if (!valid(f)) return 1;
    const float lvl = std::clamp(getp(params, n, 0, 128.0f), 0.0f, 255.0f);
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, row += 4) {
            const uint8_t v = luma(row) > lvl ? 255 : 0;
            row[0] = row[1] = row[2] = v;
        }
    }
    return 0;
}

int scanlines(XER_VideoFrame* f, const float* params, int32_t n) {
    if (!valid(f)) return 1;
    const int32_t spacing =
        static_cast<int32_t>(std::clamp(getp(params, n, 0, 4.0f), 2.0f, 16.0f));
    const float k = 1.0f - std::clamp(getp(params, n, 1, 0.5f), 0.0f, 1.0f);
    for (int32_t y = 0; y < f->height; ++y) {
        if (y % spacing != spacing - 1) continue;
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, row += 4) {
            row[0] = static_cast<uint8_t>(row[0] * k);
            row[1] = static_cast<uint8_t>(row[1] * k);
            row[2] = static_cast<uint8_t>(row[2] * k);
        }
    }
    return 0;
}

int neon_edges(XER_VideoFrame* f, const float* params, int32_t n) {
    if (!valid(f)) return 1;
    const float thr = std::clamp(getp(params, n, 0, 40.0f), 0.0f, 255.0f);
    uint8_t hue[3];
    hue_to_bgr(std::clamp(getp(params, n, 1, 300.0f), 0.0f, 360.0f), hue);

    const int32_t w = f->width, h = f->height;
    std::vector<float> lum(static_cast<size_t>(w) * h);
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x) {
            lum[static_cast<size_t>(y) * w + x] = luma(row + x * 4);
        }
    }
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x, row += 4) {
            float mag = 0.0f;
            if (x > 0 && x + 1 < w && y > 0 && y + 1 < h) {
                const float* c = lum.data() + static_cast<size_t>(y) * w + x;
                const float gx = -c[-w - 1] + c[-w + 1] - 2.0f * c[-1] +
                                 2.0f * c[1] - c[w - 1] + c[w + 1];
                const float gy = c[w - 1] + 2.0f * c[w] + c[w + 1] -
                                 c[-w - 1] - 2.0f * c[-w] - c[-w + 1];
                mag = std::sqrt(gx * gx + gy * gy);
            }
            const float t = mag > thr ? std::min(1.0f, mag / 255.0f) : 0.0f;
            row[0] = static_cast<uint8_t>(hue[0] * t);
            row[1] = static_cast<uint8_t>(hue[1] * t);
            row[2] = static_cast<uint8_t>(hue[2] * t);
        }
    }
    return 0;
}

}  // namespace fx
