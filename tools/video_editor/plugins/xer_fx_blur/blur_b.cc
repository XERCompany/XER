// Impl file — fast_gaussian, unsharp_mask, median3.
#include "effects.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace fx {
namespace {

float getp(const float* p, int32_t n, int32_t i, float def) {
    return i < n ? p[i] : def;
}

bool valid(const XER_VideoFrame* f) {
    return f && f->data && f->width > 0 && f->height > 0;
}

std::vector<uint8_t> pack(const XER_VideoFrame& f) {
    const size_t row = static_cast<size_t>(f.width) * 4;
    std::vector<uint8_t> v(row * f.height);
    for (int32_t y = 0; y < f.height; ++y)
        std::copy_n(f.data + static_cast<size_t>(y) * f.stride, row,
                    v.data() + static_cast<size_t>(y) * row);
    return v;
}

void unpack(XER_VideoFrame& f, const uint8_t* src) {
    const size_t row = static_cast<size_t>(f.width) * 4;
    for (int32_t y = 0; y < f.height; ++y)
        std::copy_n(src + static_cast<size_t>(y) * row, row,
                    f.data + static_cast<size_t>(y) * f.stride);
}

// Replicated-border sliding-window box blur: horizontal src->tmp, then
// vertical tmp->dst.
void box_blur(const uint8_t* src, uint8_t* tmp, uint8_t* dst, int32_t w,
              int32_t h, int32_t r) {
    const int32_t d = 2 * r + 1;
    const size_t row = static_cast<size_t>(w) * 4;
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t* s = src + static_cast<size_t>(y) * row;
        uint8_t* t = tmp + static_cast<size_t>(y) * row;
        std::array<int32_t, 4> acc{};
        for (int32_t x = -r; x <= r; ++x) {
            const uint8_t* p = s + std::clamp(x, 0, w - 1) * 4;
            for (int c = 0; c < 4; ++c) acc[c] += p[c];
        }
        for (int32_t x = 0; x < w; ++x) {
            for (int c = 0; c < 4; ++c)
                t[x * 4 + c] = static_cast<uint8_t>(acc[c] / d);
            const uint8_t* a = s + std::min(x + r + 1, w - 1) * 4;
            const uint8_t* b = s + std::max(x - r, 0) * 4;
            for (int c = 0; c < 4; ++c) acc[c] += a[c] - b[c];
        }
    }
    for (int32_t x = 0; x < w; ++x) {
        std::array<int32_t, 4> acc{};
        for (int32_t y = -r; y <= r; ++y) {
            const uint8_t* p =
                tmp + static_cast<size_t>(std::clamp(y, 0, h - 1)) * row +
                x * 4;
            for (int c = 0; c < 4; ++c) acc[c] += p[c];
        }
        for (int32_t y = 0; y < h; ++y) {
            uint8_t* o = dst + static_cast<size_t>(y) * row + x * 4;
            for (int c = 0; c < 4; ++c)
                o[c] = static_cast<uint8_t>(acc[c] / d);
            const uint8_t* a =
                tmp + static_cast<size_t>(std::min(y + r + 1, h - 1)) * row +
                x * 4;
            const uint8_t* b =
                tmp + static_cast<size_t>(std::max(y - r, 0)) * row + x * 4;
            for (int c = 0; c < 4; ++c) acc[c] += a[c] - b[c];
        }
    }
}

void median_pass(const uint8_t* src, uint8_t* dst, int32_t w, int32_t h) {
    const size_t row = static_cast<size_t>(w) * 4;
    for (int32_t y = 0; y < h; ++y)
        for (int32_t x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c) {
                std::array<uint8_t, 9> m{};
                int k = 0;
                for (int32_t dy = -1; dy <= 1; ++dy)
                    for (int32_t dx = -1; dx <= 1; ++dx)
                        m[k++] =
                            src[static_cast<size_t>(
                                    std::clamp(y + dy, 0, h - 1)) *
                                    row +
                                static_cast<size_t>(
                                    std::clamp(x + dx, 0, w - 1)) *
                                    4 +
                                c];
                std::nth_element(m.begin(), m.begin() + 4, m.end());
                dst[static_cast<size_t>(y) * row +
                    static_cast<size_t>(x) * 4 + c] = m[4];
            }
}

}  // namespace

int fast_gaussian(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const int32_t r = std::clamp(
        static_cast<int32_t>(std::lround(getp(p, n, 0, 6.f))), 1, 32);
    const size_t sz = static_cast<size_t>(f->width) * f->height * 4;
    std::vector<uint8_t> a = pack(*f), b(sz), tmp(sz);
    uint8_t* s = a.data();
    uint8_t* d = b.data();
    for (int i = 0; i < 3; ++i) {
        box_blur(s, tmp.data(), d, f->width, f->height, r);
        std::swap(s, d);
    }
    unpack(*f, s);
    return 0;
}

int unsharp_mask(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const float amt = std::clamp(getp(p, n, 0, 1.f), 0.f, 4.f);
    const int32_t r = std::clamp(
        static_cast<int32_t>(std::lround(getp(p, n, 1, 3.f))), 1, 16);
    const size_t sz = static_cast<size_t>(f->width) * f->height * 4;
    std::vector<uint8_t> orig = pack(*f), blur(sz), tmp(sz);
    box_blur(orig.data(), tmp.data(), blur.data(), f->width, f->height, r);
    for (size_t i = 0; i < sz; ++i) {
        const float v = orig[i] + (orig[i] - blur[i]) * amt;
        orig[i] = static_cast<uint8_t>(
            std::clamp(static_cast<int>(std::lround(v)), 0, 255));
    }
    unpack(*f, orig.data());
    return 0;
}

int median3(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!valid(f)) return 1;
    const int32_t passes = std::clamp(
        static_cast<int32_t>(std::lround(getp(p, n, 0, 1.f))), 1, 4);
    const size_t sz = static_cast<size_t>(f->width) * f->height * 4;
    std::vector<uint8_t> a = pack(*f), b(sz);
    uint8_t* s = a.data();
    uint8_t* d = b.data();
    for (int32_t i = 0; i < passes; ++i) {
        median_pass(s, d, f->width, f->height);
        std::swap(s, d);
    }
    unpack(*f, s);
    return 0;
}

}  // namespace fx
