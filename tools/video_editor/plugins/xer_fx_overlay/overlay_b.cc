#include "effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace fx {
namespace {

float param(const float* p, int32_t n, int32_t i, float dflt) {
    return i < n ? p[i] : dflt;
}

uint8_t* row(XER_VideoFrame* f, int32_t y) {
    return f->data + static_cast<int64_t>(y) * f->stride;
}

// Blends B,G,R of one pixel toward v by alpha a; alpha channel untouched.
void blend_to(uint8_t* px, uint8_t v, float a) {
    px[0] = static_cast<uint8_t>(std::lround(px[0] + (v - px[0]) * a));
    px[1] = static_cast<uint8_t>(std::lround(px[1] + (v - px[1]) * a));
    px[2] = static_cast<uint8_t>(std::lround(px[2] + (v - px[2]) * a));
}

void fill_rect(XER_VideoFrame* f, int32_t x, int32_t y, int32_t w, int32_t h,
               uint8_t v, float a) {
    const int32_t x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int32_t x1 = std::min(x + w, f->width),
                  y1 = std::min(y + h, f->height);
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint8_t* p = row(f, yy) + static_cast<int64_t>(x0) * 4;
        for (int32_t xx = x0; xx < x1; ++xx, p += 4) blend_to(p, v, a);
    }
}

}  // namespace

int grid_lines(XER_VideoFrame* f, const float* p, int32_t n) {
    const int32_t cells =
        static_cast<int32_t>(std::clamp(param(p, n, 0, 4.f), 2.f, 16.f));
    const float a = std::clamp(param(p, n, 1, 0.3f), 0.f, 1.f);
    for (int32_t i = 1; i < cells; ++i) {
        fill_rect(f, i * f->width / cells, 0, 1, f->height, 255, a);
        fill_rect(f, 0, i * f->height / cells, f->width, 1, 255, a);
    }
    return 0;
}

int border_frame(XER_VideoFrame* f, const float* p, int32_t n) {
    const int32_t t =
        static_cast<int32_t>(std::clamp(param(p, n, 0, 8.f), 0.f, 64.f));
    if (t <= 0) return 0;
    fill_rect(f, 0, 0, f->width, t, 0, 1.f);
    fill_rect(f, 0, f->height - t, f->width, t, 0, 1.f);
    fill_rect(f, 0, 0, t, f->height, 0, 1.f);
    fill_rect(f, f->width - t, 0, t, f->height, 0, 1.f);
    return 0;
}

int corner_marks(XER_VideoFrame* f, const float* p, int32_t n) {
    const int32_t len =
        static_cast<int32_t>(std::clamp(param(p, n, 0, 24.f), 4.f, 64.f));
    constexpr int32_t kT = 2;
    const int32_t w = f->width, h = f->height;
    for (int32_t sy = 0; sy < 2; ++sy)
        for (int32_t sx = 0; sx < 2; ++sx) {
            fill_rect(f, sx ? w - len : 0, sy ? h - kT : 0, len, kT, 255, 1.f);
            fill_rect(f, sx ? w - kT : 0, sy ? h - len : 0, kT, len, 255, 1.f);
        }
    return 0;
}

}  // namespace fx
