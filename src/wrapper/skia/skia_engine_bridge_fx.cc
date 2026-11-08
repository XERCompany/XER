// skia_engine_bridge_fx.cc
// FX-level image operations: transforms, adjustments, filters, drawing,
// text, masks, histograms.  All in namespace XER::Bridge::Skia.

#include "wrapper/skia/skia_engine_bridge.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

// Optional native Skia includes (mirrors skia_engine_bridge.cc detection)
#if __has_include("include/core/SkBitmap.h")
#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkPath.h"
#include "include/effects/SkImageFilters.h"
#include "include/core/SkFont.h"
#include "include/core/SkTextBlob.h"
#include "include/effects/SkGradientShader.h"
#define PS_HAS_SKIA 1
#elif __has_include(<include/core/SkBitmap.h>)
#include <include/core/SkBitmap.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkPaint.h>
#include <include/core/SkRect.h>
#include <include/core/SkPath.h>
#include <include/effects/SkImageFilters.h>
#include <include/core/SkFont.h>
#include <include/core/SkTextBlob.h>
#include <include/effects/SkGradientShader.h>
#define PS_HAS_SKIA 1
#else
#define PS_HAS_SKIA 0
#endif

namespace XER::Bridge::Skia {

// ─── Internal helpers ────────────────────────────────────────────────────────

namespace {

constexpr double kPI = 3.14159265358979323846;
constexpr double kPI2 = kPI * 2.0;

inline uint8_t Clamp8(int v) {
    return static_cast<uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v);
}
inline uint8_t Clamp8f(double v) {
    return static_cast<uint8_t>(v < 0.0 ? 0 : v > 255.0 ? 255 : static_cast<int>(v + 0.5));
}

inline uint32_t PackRGBA_ps(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (static_cast<uint32_t>(r) << 24) | (static_cast<uint32_t>(g) << 16) |
           (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}
inline uint8_t GetR_ps(uint32_t c) { return static_cast<uint8_t>((c >> 24) & 0xFF); }
inline uint8_t GetG_ps(uint32_t c) { return static_cast<uint8_t>((c >> 16) & 0xFF); }
inline uint8_t GetB_ps(uint32_t c) { return static_cast<uint8_t>((c >> 8) & 0xFF); }
inline uint8_t GetA_ps(uint32_t c) { return static_cast<uint8_t>(c & 0xFF); }

// Fast bilinear sample
inline uint32_t SampleBilinear(const std::vector<uint32_t>& src, int sw, int sh,
                                float fx, float fy) {
    const int x0 = static_cast<int>(fx);
    const int y0 = static_cast<int>(fy);
    const int x1 = std::min(x0 + 1, sw - 1);
    const int y1 = std::min(y0 + 1, sh - 1);
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);
    const int X0 = std::max(0, x0), Y0 = std::max(0, y0);
    const uint32_t c00 = src[static_cast<size_t>(Y0) * sw + X0];
    const uint32_t c10 = src[static_cast<size_t>(Y0) * sw + x1];
    const uint32_t c01 = src[static_cast<size_t>(y1) * sw + X0];
    const uint32_t c11 = src[static_cast<size_t>(y1) * sw + x1];
    auto interp = [&](uint32_t (*fn)(uint32_t)) -> uint8_t {
        const float v = (1 - ty) * ((1 - tx) * fn(c00) + tx * fn(c10)) +
                        ty * ((1 - tx) * fn(c01) + tx * fn(c11));
        return Clamp8f(v);
    };
    (void)interp;
    auto lerp_ch = [&](int shift) -> uint8_t {
        auto ch = [&](uint32_t px) -> float {
            return static_cast<float>((px >> shift) & 0xFF);
        };
        return Clamp8f((1-ty)*((1-tx)*ch(c00)+tx*ch(c10)) + ty*((1-tx)*ch(c01)+tx*ch(c11)));
    };
    const uint8_t r = lerp_ch(24), g = lerp_ch(16), b = lerp_ch(8), a = lerp_ch(0);
    return PackRGBA_ps(r, g, b, a);
}

// RGB <-> HSL (all in [0,1])
inline void RGB2HSL(float r, float g, float b, float& h, float& s, float& l) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) { h = s = 0.f; return; }
    const float d = mx - mn;
    s = l > 0.5f ? d / (2.f - mx - mn) : d / (mx + mn);
    if (mx == r)      h = (g - b) / d + (g < b ? 6.f : 0.f);
    else if (mx == g) h = (b - r) / d + 2.f;
    else              h = (r - g) / d + 4.f;
    h /= 6.f;
}

inline float HUE2RGB(float p, float q, float t) {
    if (t < 0.f) t += 1.f;
    if (t > 1.f) t -= 1.f;
    if (t < 1.f/6.f) return p + (q - p) * 6.f * t;
    if (t < 0.5f)    return q;
    if (t < 2.f/3.f) return p + (q - p) * (2.f/3.f - t) * 6.f;
    return p;
}

inline void HSL2RGB(float h, float s, float l, float& r, float& g, float& b) {
    if (s == 0.f) { r = g = b = l; return; }
    const float q = l < 0.5f ? l * (1.f + s) : l + s - l * s;
    const float p = 2.f * l - q;
    r = HUE2RGB(p, q, h + 1.f/3.f);
    g = HUE2RGB(p, q, h);
    b = HUE2RGB(p, q, h - 1.f/3.f);
}

inline float Luminance(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

// ColorDistance in RGBA space
inline int ColorDist(uint32_t a, uint32_t b_) {
    const int dr = static_cast<int>(GetR_ps(a)) - GetR_ps(b_);
    const int dg = static_cast<int>(GetG_ps(a)) - GetG_ps(b_);
    const int db = static_cast<int>(GetB_ps(a)) - GetB_ps(b_);
    return std::abs(dr) + std::abs(dg) + std::abs(db);
}

// Simple gaussian kernel generation
std::vector<double> MakeGaussianKernel1D(double sigma, int& radius_out) {
    radius_out = static_cast<int>(std::ceil(sigma * 3.0));
    if (radius_out < 1) radius_out = 1;
    const int sz = 2 * radius_out + 1;
    std::vector<double> k(static_cast<size_t>(sz));
    double sum = 0;
    for (int i = 0; i < sz; ++i) {
        const double x = i - radius_out;
        k[static_cast<size_t>(i)] = std::exp(-0.5 * x * x / (sigma * sigma));
        sum += k[static_cast<size_t>(i)];
    }
    for (auto& v : k) v /= sum;
    return k;
}

// Separate horizontal + vertical 1D convolution
void ConvolveSep(std::vector<uint32_t>* pixels, int w, int h,
                 const std::vector<double>& kh, int rh,
                 const std::vector<double>& kv, int rv) {
    const int ww = w, hh = h;
    std::vector<std::array<double, 4>> tmp(static_cast<size_t>(ww * hh));
    // Horizontal pass
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < ww; ++x) {
            std::array<double, 4> acc{};
            for (int k = -rh; k <= rh; ++k) {
                const int sx = std::max(0, std::min(ww - 1, x + k));
                const uint32_t px = (*pixels)[static_cast<size_t>(y) * ww + sx];
                const double wt = kh[static_cast<size_t>(k + rh)];
                acc[0] += GetR_ps(px) * wt;
                acc[1] += GetG_ps(px) * wt;
                acc[2] += GetB_ps(px) * wt;
                acc[3] += GetA_ps(px) * wt;
            }
            tmp[static_cast<size_t>(y) * ww + x] = acc;
        }
    }
    // Vertical pass
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < ww; ++x) {
            std::array<double, 4> acc{};
            for (int k = -rv; k <= rv; ++k) {
                const int sy = std::max(0, std::min(hh - 1, y + k));
                const auto& t = tmp[static_cast<size_t>(sy) * ww + x];
                const double wt = kv[static_cast<size_t>(k + rv)];
                acc[0] += t[0] * wt;
                acc[1] += t[1] * wt;
                acc[2] += t[2] * wt;
                acc[3] += t[3] * wt;
            }
            (*pixels)[static_cast<size_t>(y) * ww + x] =
                PackRGBA_ps(Clamp8f(acc[0]), Clamp8f(acc[1]),
                             Clamp8f(acc[2]), Clamp8f(acc[3]));
        }
    }
}

// Blend single premultiplied pixel using src-over
inline uint32_t BlendSrcOver_ps(uint32_t dst, uint32_t src) {
    const float sa = GetA_ps(src) / 255.f;
    const float da = GetA_ps(dst) / 255.f;
    const float oa = sa + da * (1.f - sa);
    if (oa < 1e-6f) return 0;
    auto ch = [&](int shift) -> uint8_t {
        const float sv = ((src >> shift) & 0xFF) / 255.f;
        const float dv = ((dst >> shift) & 0xFF) / 255.f;
        return Clamp8f(((sv * sa + dv * da * (1.f - sa)) / oa) * 255.f);
    };
    return PackRGBA_ps(ch(24), ch(16), ch(8), static_cast<uint8_t>(oa * 255.f));
}

// Normalize range for "in which tonal band" (for color balance)
inline float TonalWeight_shadow(float l)   { return (1.f - l) * (1.f - l); }
inline float TonalWeight_midtone(float l)  { const float t = l - 0.5f; return 1.f - 4.f*t*t; }
inline float TonalWeight_hilight(float l)  { return l * l; }

}  // namespace

// ===========================================================================
// Transforms
// ===========================================================================

bool ImageScale(const std::vector<uint32_t>& src, int src_w, int src_h,
                int dst_w, int dst_h,
                std::vector<uint32_t>* out,
                bool bilinear) {
    if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0 || out == nullptr) {
        return false;
    }
    out->resize(static_cast<size_t>(dst_w * dst_h));
    const float sx = static_cast<float>(src_w) / dst_w;
    const float sy = static_cast<float>(src_h) / dst_h;
    for (int dy = 0; dy < dst_h; ++dy) {
        for (int dx = 0; dx < dst_w; ++dx) {
            const float fx = (dx + 0.5f) * sx - 0.5f;
            const float fy = (dy + 0.5f) * sy - 0.5f;
            (*out)[static_cast<size_t>(dy) * dst_w + dx] =
                bilinear
                ? SampleBilinear(src, src_w, src_h, std::max(0.f, fx), std::max(0.f, fy))
                : src[static_cast<size_t>(std::clamp(static_cast<int>(fy + 0.5f), 0, src_h-1)) * src_w +
                       std::clamp(static_cast<int>(fx + 0.5f), 0, src_w-1)];
        }
    }
    return true;
}

bool ImageRotate(const std::vector<uint32_t>& src, int src_w, int src_h,
                 double degrees, uint32_t bg_color,
                 std::vector<uint32_t>* out, int* out_w, int* out_h) {
    if (out == nullptr || out_w == nullptr || out_h == nullptr) return false;
    const double rad = degrees * kPI / 180.0;
    const double cosA = std::cos(rad), sinA = std::sin(rad);
    // Compute bounding box of rotated rectangle
    const double hw = src_w * 0.5, hh = src_h * 0.5;
    const std::array<std::pair<double,double>, 4> corners = {{
        {-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}
    }};
    double minX = 1e18, maxX = -1e18, minY = 1e18, maxY = -1e18;
    for (auto [cx, cy] : corners) {
        const double rx = cx * cosA - cy * sinA;
        const double ry = cx * sinA + cy * cosA;
        minX = std::min(minX, rx); maxX = std::max(maxX, rx);
        minY = std::min(minY, ry); maxY = std::max(maxY, ry);
    }
    *out_w = static_cast<int>(std::ceil(maxX - minX));
    *out_h = static_cast<int>(std::ceil(maxY - minY));
    out->assign(static_cast<size_t>(*out_w * *out_h), bg_color);
    const double ox = *out_w * 0.5, oy = *out_h * 0.5;
    for (int y = 0; y < *out_h; ++y) {
        for (int x = 0; x < *out_w; ++x) {
            const double dx = x - ox, dy = y - oy;
            const float sx = static_cast<float>( dx * cosA + dy * sinA + hw);
            const float sy = static_cast<float>(-dx * sinA + dy * cosA + hh);
            if (sx < 0 || sy < 0 || sx >= src_w || sy >= src_h) continue;
            (*out)[static_cast<size_t>(y) * *out_w + x] =
                SampleBilinear(src, src_w, src_h, sx, sy);
        }
    }
    return true;
}

bool ImageFlipH(const std::vector<uint32_t>& src, int src_w, int src_h,
                std::vector<uint32_t>* out) {
    if (out == nullptr) return false;
    *out = src;
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < src_w / 2; ++x) {
            std::swap((*out)[static_cast<size_t>(y) * src_w + x],
                      (*out)[static_cast<size_t>(y) * src_w + (src_w - 1 - x)]);
        }
    }
    return true;
}

bool ImageFlipV(const std::vector<uint32_t>& src, int src_w, int src_h,
                std::vector<uint32_t>* out) {
    if (out == nullptr) return false;
    out->resize(src.size());
    for (int y = 0; y < src_h; ++y) {
        const int ry = src_h - 1 - y;
        std::memcpy(out->data() + static_cast<size_t>(ry) * src_w,
                    src.data()  + static_cast<size_t>(y) * src_w,
                    static_cast<size_t>(src_w) * sizeof(uint32_t));
    }
    return true;
}

bool ImageCrop(const std::vector<uint32_t>& src, int src_w, int src_h,
               int x, int y, int cw, int ch,
               std::vector<uint32_t>* out) {
    if (out == nullptr) return false;
    const int x0 = std::max(0, x),    y0 = std::max(0, y);
    const int x1 = std::min(src_w, x + std::max(0, cw));
    const int y1 = std::min(src_h, y + std::max(0, ch));
    const int ow = x1 - x0, oh = y1 - y0;
    if (ow <= 0 || oh <= 0) return false;
    out->resize(static_cast<size_t>(ow * oh));
    for (int row = 0; row < oh; ++row) {
        std::memcpy(out->data() + static_cast<size_t>(row) * ow,
                    src.data()  + static_cast<size_t>(y0 + row) * src_w + x0,
                    static_cast<size_t>(ow) * sizeof(uint32_t));
    }
    return true;
}

bool ImageComposite(std::vector<uint32_t>* dst, int dst_w, int dst_h,
                    const std::vector<uint32_t>& src, int src_w, int src_h,
                    int dx, int dy,
                    const std::string& blend_mode, float alpha) {
    if (dst == nullptr) return false;
    const float a_mul = std::max(0.f, std::min(1.f, alpha));
    for (int sy = 0; sy < src_h; ++sy) {
        const int ty = dy + sy;
        if (ty < 0 || ty >= dst_h) continue;
        for (int sx = 0; sx < src_w; ++sx) {
            const int tx = dx + sx;
            if (tx < 0 || tx >= dst_w) continue;
            uint32_t src_px = src[static_cast<size_t>(sy) * src_w + sx];
            // Modulate source alpha
            if (a_mul < 1.f) {
                const uint8_t na = Clamp8f(GetA_ps(src_px) * a_mul);
                src_px = (src_px & 0xFFFFFF00u) | na;
            }
            uint32_t& dst_px = (*dst)[static_cast<size_t>(ty) * dst_w + tx];
            // Use the general bridge blend which handles many modes
            dst_px = static_cast<uint32_t>(
                BlendModeApply(blend_mode,
                               static_cast<uint32_t>(dst_px),
                               static_cast<uint32_t>(src_px)));
        }
    }
    return true;
}

// ===========================================================================
// Colour adjustments
// ===========================================================================

bool AdjustBrightnessContrast(std::vector<uint32_t>* pixels,
                               int width, int height,
                               int brightness, int contrast) {
    if (pixels == nullptr) return false;
    // Build LUT
    std::array<uint8_t, 256> lut{};
    const double factor = (259.0 * (contrast + 255.0)) / (255.0 * (259.0 - contrast));
    for (int i = 0; i < 256; ++i) {
        const double v = factor * (i - 128.0) + 128.0 + brightness;
        lut[static_cast<size_t>(i)] = Clamp8f(v);
    }
    for (auto& px : *pixels) {
        px = PackRGBA_ps(lut[GetR_ps(px)], lut[GetG_ps(px)],
                          lut[GetB_ps(px)], GetA_ps(px));
    }
    return true;
}

bool AdjustHSL(std::vector<uint32_t>* pixels, int width, int height,
               double hue, double saturation, double lightness) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const float dh = static_cast<float>(hue / 360.0);
    const float ds = static_cast<float>(saturation / 100.0);
    const float dl = static_cast<float>(lightness / 100.0);
    for (auto& px : *pixels) {
        float r = GetR_ps(px) / 255.f;
        float g = GetG_ps(px) / 255.f;
        float b = GetB_ps(px) / 255.f;
        float h, s, l;
        RGB2HSL(r, g, b, h, s, l);
        h = h + dh;
        if (h > 1.f) h -= 1.f;
        if (h < 0.f) h += 1.f;
        s = std::max(0.f, std::min(1.f, s + ds));
        l = std::max(0.f, std::min(1.f, l + dl));
        HSL2RGB(h, s, l, r, g, b);
        px = PackRGBA_ps(Clamp8f(r*255.f), Clamp8f(g*255.f),
                          Clamp8f(b*255.f), GetA_ps(px));
    }
    return true;
}

bool AdjustExposure(std::vector<uint32_t>* pixels, int width, int height,
                    double exposure, double gamma) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const double exp_mul = std::pow(2.0, exposure);
    const double inv_gamma = gamma > 0.0 ? 1.0 / gamma : 1.0;
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        double v = (i / 255.0) * exp_mul;
        v = std::pow(std::max(0.0, v), inv_gamma);
        lut[static_cast<size_t>(i)] = Clamp8f(v * 255.0);
    }
    for (auto& px : *pixels) {
        px = PackRGBA_ps(lut[GetR_ps(px)], lut[GetG_ps(px)],
                          lut[GetB_ps(px)], GetA_ps(px));
    }
    return true;
}

bool AdjustLevels(std::vector<uint32_t>* pixels, int width, int height,
                  int in_black, int in_white, double gamma,
                  int out_black, int out_white) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const double in_range  = std::max(1.0, static_cast<double>(in_white - in_black));
    const double out_range = static_cast<double>(out_white - out_black);
    const double inv_gamma = gamma > 0.0 ? 1.0 / gamma : 1.0;
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        double v = (i - in_black) / in_range;
        v = std::max(0.0, std::min(1.0, v));
        v = std::pow(v, inv_gamma);
        v = v * out_range + out_black;
        lut[static_cast<size_t>(i)] = Clamp8f(v);
    }
    for (auto& px : *pixels) {
        px = PackRGBA_ps(lut[GetR_ps(px)], lut[GetG_ps(px)],
                          lut[GetB_ps(px)], GetA_ps(px));
    }
    return true;
}

bool AdjustCurves(std::vector<uint32_t>* pixels, int width, int height,
                  const uint8_t* lut_r, const uint8_t* lut_g,
                  const uint8_t* lut_b, const uint8_t* lut_a) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        const uint8_t r = lut_r ? lut_r[GetR_ps(px)] : GetR_ps(px);
        const uint8_t g = lut_g ? lut_g[GetG_ps(px)] : GetG_ps(px);
        const uint8_t b = lut_b ? lut_b[GetB_ps(px)] : GetB_ps(px);
        const uint8_t a = lut_a ? lut_a[GetA_ps(px)] : GetA_ps(px);
        px = PackRGBA_ps(r, g, b, a);
    }
    return true;
}

bool AdjustColorBalance(std::vector<uint32_t>* pixels, int width, int height,
                        int shadow_r,   int shadow_g,   int shadow_b,
                        int midtone_r,  int midtone_g,  int midtone_b,
                        int hilight_r,  int hilight_g,  int hilight_b) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        float r = GetR_ps(px) / 255.f;
        float g = GetG_ps(px) / 255.f;
        float b = GetB_ps(px) / 255.f;
        const float l = Luminance(r, g, b);
        const float ws = TonalWeight_shadow(l);
        const float wm = TonalWeight_midtone(l);
        const float wh = TonalWeight_hilight(l);
        r = std::max(0.f, std::min(1.f, r + (shadow_r*ws + midtone_r*wm + hilight_r*wh) / 255.f));
        g = std::max(0.f, std::min(1.f, g + (shadow_g*ws + midtone_g*wm + hilight_g*wh) / 255.f));
        b = std::max(0.f, std::min(1.f, b + (shadow_b*ws + midtone_b*wm + hilight_b*wh) / 255.f));
        px = PackRGBA_ps(Clamp8f(r*255.f), Clamp8f(g*255.f),
                          Clamp8f(b*255.f), GetA_ps(px));
    }
    return true;
}

bool AdjustVibrance(std::vector<uint32_t>* pixels, int width, int height,
                    double vibrance) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const float vib = static_cast<float>(vibrance / 100.0);
    for (auto& px : *pixels) {
        float r = GetR_ps(px) / 255.f;
        float g = GetG_ps(px) / 255.f;
        float b = GetB_ps(px) / 255.f;
        float h, s, l;
        RGB2HSL(r, g, b, h, s, l);
        // Protect skin tones (reddish-orange hue ≈ 0-0.1 and 0.9-1.0 in [0,1])
        const float skin_prot = (h < 0.1f || h > 0.9f) ? 0.3f : 1.0f;
        const float boost = vib * skin_prot * (1.f - s);
        s = std::max(0.f, std::min(1.f, s + boost));
        HSL2RGB(h, s, l, r, g, b);
        px = PackRGBA_ps(Clamp8f(r*255.f), Clamp8f(g*255.f),
                          Clamp8f(b*255.f), GetA_ps(px));
    }
    return true;
}

bool AdjustChannelMixer(std::vector<uint32_t>* pixels, int width, int height,
                        const ChannelMixerCoeff& cr,
                        const ChannelMixerCoeff& cg,
                        const ChannelMixerCoeff& cb) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        const double r = GetR_ps(px), g = GetG_ps(px), bv = GetB_ps(px);
        const double nr = cr.r*r + cr.g*g + cr.b*bv + cr.constant*255.0;
        const double ng = cg.r*r + cg.g*g + cg.b*bv + cg.constant*255.0;
        const double nb = cb.r*r + cb.g*g + cb.b*bv + cb.constant*255.0;
        px = PackRGBA_ps(Clamp8f(nr), Clamp8f(ng), Clamp8f(nb), GetA_ps(px));
    }
    return true;
}

bool ColorReplace(std::vector<uint32_t>* pixels, int width, int height,
                  uint32_t from_color, uint32_t to_color, int tolerance) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        if (ColorDist(px, from_color) <= tolerance) {
            px = to_color;
        }
    }
    return true;
}

bool AdjustInvert(std::vector<uint32_t>* pixels, int width, int height) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        px = PackRGBA_ps(
            static_cast<uint8_t>(255 - GetR_ps(px)),
            static_cast<uint8_t>(255 - GetG_ps(px)),
            static_cast<uint8_t>(255 - GetB_ps(px)),
            GetA_ps(px));
    }
    return true;
}

bool AdjustGrayscale(std::vector<uint32_t>* pixels, int width, int height) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        const uint8_t grey = Clamp8f(Luminance(
            GetR_ps(px)/255.f, GetG_ps(px)/255.f, GetB_ps(px)/255.f) * 255.f);
        px = PackRGBA_ps(grey, grey, grey, GetA_ps(px));
    }
    return true;
}

bool AdjustSepia(std::vector<uint32_t>* pixels, int width, int height,
                 double intensity) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const float t = static_cast<float>(std::max(0.0, std::min(1.0, intensity)));
    for (auto& px : *pixels) {
        const float r = GetR_ps(px)/255.f, g = GetG_ps(px)/255.f, b = GetB_ps(px)/255.f;
        const float sr = std::min(1.f, r*0.393f + g*0.769f + b*0.189f);
        const float sg = std::min(1.f, r*0.349f + g*0.686f + b*0.168f);
        const float sb = std::min(1.f, r*0.272f + g*0.534f + b*0.131f);
        px = PackRGBA_ps(Clamp8f((sr*t + r*(1-t))*255.f),
                          Clamp8f((sg*t + g*(1-t))*255.f),
                          Clamp8f((sb*t + b*(1-t))*255.f),
                          GetA_ps(px));
    }
    return true;
}

bool AdjustThreshold(std::vector<uint32_t>* pixels, int width, int height,
                     int value) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    for (auto& px : *pixels) {
        const float l = Luminance(GetR_ps(px)/255.f, GetG_ps(px)/255.f, GetB_ps(px)/255.f);
        const uint8_t v = static_cast<uint8_t>(l * 255.f) >= value ? 255 : 0;
        px = PackRGBA_ps(v, v, v, GetA_ps(px));
    }
    return true;
}

bool AdjustPosterize(std::vector<uint32_t>* pixels, int width, int height,
                     int levels) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const int lv = std::max(2, std::min(255, levels));
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        lut[static_cast<size_t>(i)] = static_cast<uint8_t>(
            std::round(std::round(i * (lv - 1.0) / 255.0) * 255.0 / (lv - 1.0)));
    }
    for (auto& px : *pixels) {
        px = PackRGBA_ps(lut[GetR_ps(px)], lut[GetG_ps(px)],
                          lut[GetB_ps(px)], GetA_ps(px));
    }
    return true;
}

bool AdjustOpacity(std::vector<uint32_t>* pixels, int width, int height,
                   float opacity) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const float op = std::max(0.f, std::min(1.f, opacity));
    for (auto& px : *pixels) {
        px = (px & 0xFFFFFF00u) | static_cast<uint8_t>(GetA_ps(px) * op);
    }
    return true;
}

bool AdjustSelectiveColor(std::vector<uint32_t>* pixels, int width, int height,
                          const std::string& range,
                          int dc, int dm, int dy, int dk) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    // Convert CMYK deltas to RGB impact
    for (auto& px : *pixels) {
        float r = GetR_ps(px)/255.f, g = GetG_ps(px)/255.f, bv = GetB_ps(px)/255.f;
        float h, s, l;
        RGB2HSL(r, g, bv, h, s, l);
        // Determine weight based on range
        float w = 0.f;
        const float hd = h * 360.f;  // 0-360
        if (range == "reds")    w = (hd < 30 || hd >= 330) ? 1.f : 0.f;
        else if (range == "yellows")  w = (hd >= 30  && hd < 90)  ? 1.f : 0.f;
        else if (range == "greens")   w = (hd >= 90  && hd < 150) ? 1.f : 0.f;
        else if (range == "cyans")    w = (hd >= 150 && hd < 210) ? 1.f : 0.f;
        else if (range == "blues")    w = (hd >= 210 && hd < 270) ? 1.f : 0.f;
        else if (range == "magentas") w = (hd >= 270 && hd < 330) ? 1.f : 0.f;
        else if (range == "whites")   w = (l > 0.75f && s < 0.25f) ? 1.f : 0.f;
        else if (range == "neutrals") w = (l > 0.25f && l < 0.75f && s < 0.25f) ? 1.f : 0.f;
        else if (range == "blacks")   w = (l < 0.25f && s < 0.25f) ? 1.f : 0.f;
        else w = 1.f;  // "all"
        if (w < 1e-4f) continue;
        // CMYK adjustments -> RGB
        const float fcm = dc / 100.f * w;
        const float fm  = dm / 100.f * w;
        const float fy  = dy / 100.f * w;
        const float fk  = dk / 100.f * w;
        // CMYK to RGB: R = (1-C)(1-K), etc.
        float cr = 1.f - r, cg = 1.f - g, cb = 1.f - bv;
        cr = std::max(0.f, std::min(1.f, cr + fcm));
        cg = std::max(0.f, std::min(1.f, cg + fm));
        cb = std::max(0.f, std::min(1.f, cb + fy));
        const float k = std::max(0.f, std::min(1.f, fk));
        r  = std::max(0.f, std::min(1.f, (1.f - cr) * (1.f - k)));
        g  = std::max(0.f, std::min(1.f, (1.f - cg) * (1.f - k)));
        bv = std::max(0.f, std::min(1.f, (1.f - cb) * (1.f - k)));
        px = PackRGBA_ps(Clamp8f(r*255.f), Clamp8f(g*255.f),
                          Clamp8f(bv*255.f), GetA_ps(px));
    }
    return true;
}

// ===========================================================================
// Filters
// ===========================================================================

bool FilterBlur(std::vector<uint32_t>* pixels, int width, int height,
                int radius_x, int radius_y) {
    if (pixels == nullptr || width <= 0 || height <= 0) return false;
    // Horizontal box blur
    std::vector<uint32_t> tmp(static_cast<size_t>(width * height));
    const double inv_rx = 1.0 / (2 * radius_x + 1);
    for (int y = 0; y < height; ++y) {
        double ar = 0, ag = 0, ab = 0, aa = 0;
        // Accumulate first window
        for (int k = -radius_x; k <= radius_x; ++k) {
            const int sx = std::max(0, std::min(width - 1, k));
            const uint32_t p = (*pixels)[static_cast<size_t>(y) * width + sx];
            ar += GetR_ps(p); ag += GetG_ps(p);
            ab += GetB_ps(p); aa += GetA_ps(p);
        }
        for (int x = 0; x < width; ++x) {
            tmp[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(Clamp8f(ar*inv_rx), Clamp8f(ag*inv_rx),
                             Clamp8f(ab*inv_rx), Clamp8f(aa*inv_rx));
            const int rem = std::max(0, std::min(width - 1, x - radius_x));
            const int add = std::min(width - 1, x + radius_x + 1);
            const uint32_t pr = (*pixels)[static_cast<size_t>(y) * width + rem];
            const uint32_t pa = (*pixels)[static_cast<size_t>(y) * width + add];
            ar += GetR_ps(pa) - GetR_ps(pr);
            ag += GetG_ps(pa) - GetG_ps(pr);
            ab += GetB_ps(pa) - GetB_ps(pr);
            aa += GetA_ps(pa) - GetA_ps(pr);
        }
    }
    // Vertical box blur
    const double inv_ry = 1.0 / (2 * radius_y + 1);
    for (int x = 0; x < width; ++x) {
        double ar = 0, ag = 0, ab = 0, aa = 0;
        for (int k = -radius_y; k <= radius_y; ++k) {
            const int sy = std::max(0, std::min(height - 1, k));
            const uint32_t p = tmp[static_cast<size_t>(sy) * width + x];
            ar += GetR_ps(p); ag += GetG_ps(p);
            ab += GetB_ps(p); aa += GetA_ps(p);
        }
        for (int y = 0; y < height; ++y) {
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(Clamp8f(ar*inv_ry), Clamp8f(ag*inv_ry),
                             Clamp8f(ab*inv_ry), Clamp8f(aa*inv_ry));
            const int rem = std::max(0, std::min(height - 1, y - radius_y));
            const int add = std::min(height - 1, y + radius_y + 1);
            const uint32_t pr = tmp[static_cast<size_t>(rem) * width + x];
            const uint32_t pa = tmp[static_cast<size_t>(add) * width + x];
            ar += GetR_ps(pa) - GetR_ps(pr);
            ag += GetG_ps(pa) - GetG_ps(pr);
            ab += GetB_ps(pa) - GetB_ps(pr);
            aa += GetA_ps(pa) - GetA_ps(pr);
        }
    }
    return true;
}

bool FilterGaussianBlur(std::vector<uint32_t>* pixels, int width, int height,
                        double sigma_x, double sigma_y) {
    if (pixels == nullptr) return false;
    int rx = 0, ry = 0;
    const auto kx = MakeGaussianKernel1D(sigma_x, rx);
    const auto ky = MakeGaussianKernel1D(sigma_y, ry);
    ConvolveSep(pixels, width, height, kx, rx, ky, ry);
    return true;
}

bool FilterMotionBlur(std::vector<uint32_t>* pixels, int width, int height,
                      double angle, int distance) {
    if (pixels == nullptr || distance < 1) return false;
    const double rad = angle * kPI / 180.0;
    const double dx = std::cos(rad), dy = std::sin(rad);
    const std::vector<uint32_t> src = *pixels;
    const double inv = 1.0 / distance;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double ar = 0, ag = 0, ab = 0, aa = 0;
            for (int k = 0; k < distance; ++k) {
                const int sx = std::max(0, std::min(width  - 1, static_cast<int>(x + k * dx)));
                const int sy = std::max(0, std::min(height - 1, static_cast<int>(y + k * dy)));
                const uint32_t p = src[static_cast<size_t>(sy) * width + sx];
                ar += GetR_ps(p); ag += GetG_ps(p);
                ab += GetB_ps(p); aa += GetA_ps(p);
            }
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(Clamp8f(ar*inv), Clamp8f(ag*inv),
                             Clamp8f(ab*inv), Clamp8f(aa*inv));
        }
    }
    return true;
}

bool FilterRadialBlur(std::vector<uint32_t>* pixels, int width, int height,
                      double amount, bool zoom_mode) {
    if (pixels == nullptr || amount <= 0) return false;
    const std::vector<uint32_t> src = *pixels;
    const float cx = width  * 0.5f, cy = height * 0.5f;
    const int samples = std::max(2, std::min(32, static_cast<int>(amount)));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double ar = 0, ag = 0, ab = 0, aa = 0;
            const float dx = x - cx, dy2 = y - cy;
            const float dist = std::sqrt(dx*dx + dy2*dy2);
            for (int k = 0; k < samples; ++k) {
                float sx, sy;
                if (zoom_mode) {
                    const float t = 1.f + (k / static_cast<float>(samples)) * static_cast<float>(amount) / 100.f;
                    sx = cx + dx / t;
                    sy = cy + dy2 / t;
                } else {
                    const float a = dist > 0.f ? k * static_cast<float>(amount) / (samples * dist * 360.f) * kPI2 : 0.f;
                    sx = cx + dx * std::cos(a) - dy2 * std::sin(a);
                    sy = cy + dx * std::sin(a) + dy2 * std::cos(a);
                }
                const uint32_t p = SampleBilinear(src, width, height,
                    std::max(0.f, std::min(static_cast<float>(width - 1), sx)),
                    std::max(0.f, std::min(static_cast<float>(height - 1), sy)));
                ar += GetR_ps(p); ag += GetG_ps(p);
                ab += GetB_ps(p); aa += GetA_ps(p);
            }
            const double inv = 1.0 / samples;
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(Clamp8f(ar*inv), Clamp8f(ag*inv),
                             Clamp8f(ab*inv), Clamp8f(aa*inv));
        }
    }
    return true;
}

bool FilterSharpen(std::vector<uint32_t>* pixels, int width, int height,
                   double amount, double sigma, int threshold) {
    if (pixels == nullptr) return false;
    std::vector<uint32_t> blurred = *pixels;
    FilterGaussianBlur(&blurred, width, height, sigma, sigma);
    for (size_t i = 0; i < pixels->size(); ++i) {
        uint32_t& orig = (*pixels)[i];
        const uint32_t blur = blurred[i];
        auto ch = [&](int shift) -> uint8_t {
            const int o = (orig >> shift) & 0xFF;
            const int b = (blur >> shift) & 0xFF;
            const int diff = o - b;
            if (std::abs(diff) < threshold) return static_cast<uint8_t>(o);
            return Clamp8(o + static_cast<int>(diff * amount));
        };
        orig = PackRGBA_ps(ch(24), ch(16), ch(8), static_cast<uint8_t>(orig & 0xFF));
    }
    return true;
}

bool FilterEmboss(std::vector<uint32_t>* pixels, int width, int height,
                  double angle, double strength) {
    if (pixels == nullptr) return false;
    const double rad = angle * kPI / 180.0;
    const int kx = static_cast<int>(std::round(std::cos(rad)));
    const int ky = static_cast<int>(std::round(std::sin(rad)));
    const std::vector<uint32_t> src = *pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int nx = std::max(0, std::min(width  - 1, x + kx));
            const int ny = std::max(0, std::min(height - 1, y + ky));
            const int px0 = std::max(0, std::min(width  - 1, x - kx));
            const int py0 = std::max(0, std::min(height - 1, y - ky));
            const uint32_t c1 = src[static_cast<size_t>(ny) * width + nx];
            const uint32_t c2 = src[static_cast<size_t>(py0) * width + px0];
            auto diff_ch = [&](int shift) -> int {
                return static_cast<int>((c1 >> shift) & 0xFF) -
                       static_cast<int>((c2 >> shift) & 0xFF);
            };
            const int dr = diff_ch(24), dg = diff_ch(16), db = diff_ch(8);
            const int v = static_cast<int>(128 + strength * (dr + dg + db) / 3.0);
            const uint8_t g = Clamp8(v);
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(g, g, g, GetA_ps(src[static_cast<size_t>(y) * width + x]));
        }
    }
    return true;
}

bool FilterEdgeDetect(std::vector<uint32_t>* pixels, int width, int height) {
    if (pixels == nullptr) return false;
    const std::vector<uint32_t> src = *pixels;
    const int Kx[3][3] = {{-1,0,1},{-2,0,2},{-1,0,1}};
    const int Ky[3][3] = {{-1,-2,-1},{0,0,0},{1,2,1}};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int gxr = 0, gyr = 0, gxg = 0, gyg = 0, gxb = 0, gyb = 0;
            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    const int sx = std::max(0, std::min(width  - 1, x + kx));
                    const int sy = std::max(0, std::min(height - 1, y + ky));
                    const uint32_t p = src[static_cast<size_t>(sy) * width + sx];
                    gxr += Kx[ky+1][kx+1] * GetR_ps(p);
                    gyr += Ky[ky+1][kx+1] * GetR_ps(p);
                    gxg += Kx[ky+1][kx+1] * GetG_ps(p);
                    gyg += Ky[ky+1][kx+1] * GetG_ps(p);
                    gxb += Kx[ky+1][kx+1] * GetB_ps(p);
                    gyb += Ky[ky+1][kx+1] * GetB_ps(p);
                }
            }
            const uint8_t r = Clamp8(static_cast<int>(std::sqrt(gxr*gxr + gyr*gyr)));
            const uint8_t g = Clamp8(static_cast<int>(std::sqrt(gxg*gxg + gyg*gyg)));
            const uint8_t b = Clamp8(static_cast<int>(std::sqrt(gxb*gxb + gyb*gyb)));
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(r, g, b, GetA_ps(src[static_cast<size_t>(y) * width + x]));
        }
    }
    return true;
}

bool FilterAddNoise(std::vector<uint32_t>* pixels, int width, int height,
                    int amount, bool gaussian, bool monochrome) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    std::mt19937 rng(42);
    std::normal_distribution<float> ndist(0.f, static_cast<float>(amount));
    std::uniform_int_distribution<int> udist(-amount, amount);
    for (auto& px : *pixels) {
        const int nr = gaussian ? static_cast<int>(ndist(rng)) : udist(rng);
        const int ng = monochrome ? nr : (gaussian ? static_cast<int>(ndist(rng)) : udist(rng));
        const int nb = monochrome ? nr : (gaussian ? static_cast<int>(ndist(rng)) : udist(rng));
        px = PackRGBA_ps(Clamp8(GetR_ps(px) + nr),
                          Clamp8(GetG_ps(px) + ng),
                          Clamp8(GetB_ps(px) + nb),
                          GetA_ps(px));
    }
    return true;
}

bool FilterMedian(std::vector<uint32_t>* pixels, int width, int height, int radius) {
    if (pixels == nullptr || radius < 1) return false;
    const std::vector<uint32_t> src = *pixels;
    const int ksize = (2*radius+1) * (2*radius+1);
    std::vector<uint8_t> vr(static_cast<size_t>(ksize));
    std::vector<uint8_t> vg(static_cast<size_t>(ksize));
    std::vector<uint8_t> vb(static_cast<size_t>(ksize));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int cnt = 0;
            for (int ky = -radius; ky <= radius; ++ky) {
                for (int kx = -radius; kx <= radius; ++kx) {
                    const int sx = std::max(0, std::min(width  - 1, x + kx));
                    const int sy = std::max(0, std::min(height - 1, y + ky));
                    const uint32_t p = src[static_cast<size_t>(sy) * width + sx];
                    vr[static_cast<size_t>(cnt)] = GetR_ps(p);
                    vg[static_cast<size_t>(cnt)] = GetG_ps(p);
                    vb[static_cast<size_t>(cnt)] = GetB_ps(p);
                    ++cnt;
                }
            }
            std::nth_element(vr.begin(), vr.begin() + cnt/2, vr.begin() + cnt);
            std::nth_element(vg.begin(), vg.begin() + cnt/2, vg.begin() + cnt);
            std::nth_element(vb.begin(), vb.begin() + cnt/2, vb.begin() + cnt);
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(vr[static_cast<size_t>(cnt/2)],
                             vg[static_cast<size_t>(cnt/2)],
                             vb[static_cast<size_t>(cnt/2)],
                             GetA_ps(src[static_cast<size_t>(y) * width + x]));
        }
    }
    return true;
}

bool FilterPixelate(std::vector<uint32_t>* pixels, int width, int height,
                    int cell_size) {
    if (pixels == nullptr || cell_size < 2) return false;
    for (int y = 0; y < height; y += cell_size) {
        for (int x = 0; x < width; x += cell_size) {
            const uint32_t rep = (*pixels)[static_cast<size_t>(y) * width + x];
            for (int cy = y; cy < std::min(height, y + cell_size); ++cy) {
                for (int cx = x; cx < std::min(width, x + cell_size); ++cx) {
                    (*pixels)[static_cast<size_t>(cy) * width + cx] = rep;
                }
            }
        }
    }
    return true;
}

bool FilterOilPaint(std::vector<uint32_t>* pixels, int width, int height,
                    int brush_size, int intensity) {
    if (pixels == nullptr || brush_size < 1 || intensity < 1) return false;
    const std::vector<uint32_t> src = *pixels;
    const int bins = std::max(2, std::min(255, intensity));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::vector<int> cnt(static_cast<size_t>(bins), 0);
            std::vector<std::array<long, 3>> sum(static_cast<size_t>(bins));
            for (auto& s : sum) s = {0, 0, 0};
            for (int ky = -brush_size; ky <= brush_size; ++ky) {
                for (int kx = -brush_size; kx <= brush_size; ++kx) {
                    const int sx = std::max(0, std::min(width  - 1, x + kx));
                    const int sy = std::max(0, std::min(height - 1, y + ky));
                    const uint32_t p = src[static_cast<size_t>(sy) * width + sx];
                    const int lum = static_cast<int>(
                        Luminance(GetR_ps(p)/255.f, GetG_ps(p)/255.f, GetB_ps(p)/255.f) * (bins-1));
                    const size_t b = static_cast<size_t>(std::max(0, std::min(bins-1, lum)));
                    cnt[b]++;
                    sum[b][0] += GetR_ps(p);
                    sum[b][1] += GetG_ps(p);
                    sum[b][2] += GetB_ps(p);
                }
            }
            int max_cnt = 0, max_b = 0;
            for (int b = 0; b < bins; ++b) {
                if (cnt[static_cast<size_t>(b)] > max_cnt) {
                    max_cnt = cnt[static_cast<size_t>(b)];
                    max_b = b;
                }
            }
            const long n = max_cnt > 0 ? max_cnt : 1;
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(
                    static_cast<uint8_t>(sum[static_cast<size_t>(max_b)][0] / n),
                    static_cast<uint8_t>(sum[static_cast<size_t>(max_b)][1] / n),
                    static_cast<uint8_t>(sum[static_cast<size_t>(max_b)][2] / n),
                    GetA_ps(src[static_cast<size_t>(y) * width + x]));
        }
    }
    return true;
}

bool FilterVignette(std::vector<uint32_t>* pixels, int width, int height,
                    double strength, double feather) {
    if (pixels == nullptr) return false;
    const float cx = width  * 0.5f, cy = height * 0.5f;
    const float maxd = std::sqrt(cx*cx + cy*cy);
    const float f = static_cast<float>(std::max(0.01, feather));
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float dx = x - cx, dy = y - cy;
            const float dist = std::sqrt(dx*dx + dy*dy) / maxd;
            const float edge = 1.f - static_cast<float>(strength);
            const float t = std::max(0.f, std::min(1.f, (dist - edge) / f));
            const float darkening = 1.f - t;
            uint32_t& px = (*pixels)[static_cast<size_t>(y) * width + x];
            px = PackRGBA_ps(Clamp8f(GetR_ps(px) * darkening),
                              Clamp8f(GetG_ps(px) * darkening),
                              Clamp8f(GetB_ps(px) * darkening),
                              GetA_ps(px));
        }
    }
    return true;
}

bool FilterChromaticAberration(std::vector<uint32_t>* pixels, int width, int height,
                                int red_offset_x, int red_offset_y,
                                int blue_offset_x, int blue_offset_y) {
    if (pixels == nullptr) return false;
    const std::vector<uint32_t> src = *pixels;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int rxs = std::max(0, std::min(width  - 1, x + red_offset_x));
            const int rys = std::max(0, std::min(height - 1, y + red_offset_y));
            const int bxs = std::max(0, std::min(width  - 1, x + blue_offset_x));
            const int bys = std::max(0, std::min(height - 1, y + blue_offset_y));
            const uint32_t pr = src[static_cast<size_t>(rys) * width + rxs];
            const uint32_t pg = src[static_cast<size_t>(y)   * width + x];
            const uint32_t pb = src[static_cast<size_t>(bys) * width + bxs];
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(GetR_ps(pr), GetG_ps(pg), GetB_ps(pb), GetA_ps(pg));
        }
    }
    return true;
}

bool FilterConvolve(std::vector<uint32_t>* pixels, int width, int height,
                    const std::vector<double>& kernel, int kw, int kh,
                    double divisor, double bias) {
    if (pixels == nullptr || kernel.empty()) return false;
    const std::vector<uint32_t> src = *pixels;
    const int rx = kw / 2, ry = kh / 2;
    const double inv = divisor != 0.0 ? 1.0 / divisor : 1.0;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            double ar = 0, ag = 0, ab = 0;
            for (int ky = 0; ky < kh; ++ky) {
                for (int kx = 0; kx < kw; ++kx) {
                    const int sx = std::max(0, std::min(width  - 1, x + kx - rx));
                    const int sy = std::max(0, std::min(height - 1, y + ky - ry));
                    const uint32_t p = src[static_cast<size_t>(sy) * width + sx];
                    const double wt = kernel[static_cast<size_t>(ky) * kw + kx];
                    ar += GetR_ps(p) * wt;
                    ag += GetG_ps(p) * wt;
                    ab += GetB_ps(p) * wt;
                }
            }
            (*pixels)[static_cast<size_t>(y) * width + x] =
                PackRGBA_ps(Clamp8f(ar * inv + bias),
                             Clamp8f(ag * inv + bias),
                             Clamp8f(ab * inv + bias),
                             GetA_ps(src[static_cast<size_t>(y) * width + x]));
        }
    }
    return true;
}

// ===========================================================================
// Drawing enhancements
// ===========================================================================

bool RasterLinearGradient(std::vector<uint32_t>* pixels, int width, int height,
                          float x0, float y0, float x1, float y1,
                          const GradientStops& stops,
                          const std::string& blend_mode) {
    if (pixels == nullptr || stops.empty()) return false;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len2 = dx*dx + dy*dy;
    if (len2 < 1e-6f) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float t = std::max(0.f, std::min(1.f,
                ((x - x0) * dx + (y - y0) * dy) / len2));
            // Find enclosing stops
            uint32_t col = stops.back().color;
            for (size_t s = 1; s < stops.size(); ++s) {
                if (t <= stops[s].pos) {
                    const float range = stops[s].pos - stops[s-1].pos;
                    const float tt = range > 0.f ? (t - stops[s-1].pos) / range : 0.f;
                    const uint32_t ca = stops[s-1].color, cb = stops[s].color;
                    col = PackRGBA_ps(
                        Clamp8f(GetR_ps(ca)*(1-tt) + GetR_ps(cb)*tt),
                        Clamp8f(GetG_ps(ca)*(1-tt) + GetG_ps(cb)*tt),
                        Clamp8f(GetB_ps(ca)*(1-tt) + GetB_ps(cb)*tt),
                        Clamp8f(GetA_ps(ca)*(1-tt) + GetA_ps(cb)*tt));
                    break;
                }
            }
            uint32_t& dst = (*pixels)[static_cast<size_t>(y) * width + x];
            dst = static_cast<uint32_t>(BlendModeApply(blend_mode, dst, col));
        }
    }
    return true;
}

bool RasterRadialGradient(std::vector<uint32_t>* pixels, int width, int height,
                          float cx, float cy, float radius,
                          const GradientStops& stops,
                          const std::string& blend_mode) {
    if (pixels == nullptr || stops.empty() || radius <= 0) return false;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const float dx = x - cx, dy2 = y - cy;
            const float t = std::max(0.f, std::min(1.f,
                std::sqrt(dx*dx + dy2*dy2) / radius));
            uint32_t col = stops.back().color;
            for (size_t s = 1; s < stops.size(); ++s) {
                if (t <= stops[s].pos) {
                    const float range = stops[s].pos - stops[s-1].pos;
                    const float tt = range > 0.f ? (t - stops[s-1].pos) / range : 0.f;
                    const uint32_t ca = stops[s-1].color, cb = stops[s].color;
                    col = PackRGBA_ps(
                        Clamp8f(GetR_ps(ca)*(1-tt) + GetR_ps(cb)*tt),
                        Clamp8f(GetG_ps(ca)*(1-tt) + GetG_ps(cb)*tt),
                        Clamp8f(GetB_ps(ca)*(1-tt) + GetB_ps(cb)*tt),
                        Clamp8f(GetA_ps(ca)*(1-tt) + GetA_ps(cb)*tt));
                    break;
                }
            }
            uint32_t& dst = (*pixels)[static_cast<size_t>(y) * width + x];
            dst = static_cast<uint32_t>(BlendModeApply(blend_mode, dst, col));
        }
    }
    return true;
}

bool RasterSweepGradient(std::vector<uint32_t>* pixels, int width, int height,
                         float cx, float cy, float start_angle,
                         const GradientStops& stops,
                         const std::string& blend_mode) {
    if (pixels == nullptr || stops.empty()) return false;
    const float start_rad = start_angle * static_cast<float>(kPI) / 180.f;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float angle = std::atan2(static_cast<float>(y) - cy,
                                     static_cast<float>(x) - cx) - start_rad;
            while (angle < 0.f) angle += static_cast<float>(kPI2);
            while (angle >= static_cast<float>(kPI2)) angle -= static_cast<float>(kPI2);
            const float t = angle / static_cast<float>(kPI2);
            uint32_t col = stops.back().color;
            for (size_t s = 1; s < stops.size(); ++s) {
                if (t <= stops[s].pos) {
                    const float range = stops[s].pos - stops[s-1].pos;
                    const float tt = range > 0.f ? (t - stops[s-1].pos) / range : 0.f;
                    const uint32_t ca = stops[s-1].color, cb = stops[s].color;
                    col = PackRGBA_ps(
                        Clamp8f(GetR_ps(ca)*(1-tt) + GetR_ps(cb)*tt),
                        Clamp8f(GetG_ps(ca)*(1-tt) + GetG_ps(cb)*tt),
                        Clamp8f(GetB_ps(ca)*(1-tt) + GetB_ps(cb)*tt),
                        Clamp8f(GetA_ps(ca)*(1-tt) + GetA_ps(cb)*tt));
                    break;
                }
            }
            uint32_t& dst = (*pixels)[static_cast<size_t>(y) * width + x];
            dst = static_cast<uint32_t>(BlendModeApply(blend_mode, dst, col));
        }
    }
    return true;
}

bool RasterFloodFill(std::vector<uint32_t>* pixels, int width, int height,
                     int seed_x, int seed_y,
                     uint32_t fill_color, int tolerance) {
    if (pixels == nullptr || seed_x < 0 || seed_x >= width || seed_y < 0 || seed_y >= height) {
        return false;
    }
    const uint32_t seed_color = (*pixels)[static_cast<size_t>(seed_y) * width + seed_x];
    if (ColorDist(seed_color, fill_color) == 0 && tolerance == 0) return true;
    std::vector<bool> visited(static_cast<size_t>(width * height), false);
    std::queue<std::pair<int,int>> q;
    q.push({seed_x, seed_y});
    visited[static_cast<size_t>(seed_y) * width + seed_x] = true;
    while (!q.empty()) {
        auto [cx, cy] = q.front(); q.pop();
        (*pixels)[static_cast<size_t>(cy) * width + cx] = fill_color;
        const int dx4[4] = {1,-1,0,0};
        const int dy4[4] = {0,0,1,-1};
        for (int d = 0; d < 4; ++d) {
            const int nx = cx + dx4[d], ny = cy + dy4[d];
            if (nx < 0 || nx >= width || ny < 0 || ny >= height) continue;
            if (visited[static_cast<size_t>(ny) * width + nx]) continue;
            const uint32_t ncol = (*pixels)[static_cast<size_t>(ny) * width + nx];
            if (ColorDist(ncol, seed_color) <= tolerance) {
                visited[static_cast<size_t>(ny) * width + nx] = true;
                q.push({nx, ny});
            }
        }
    }
    return true;
}

bool RasterDrawRoundRect(std::vector<uint32_t>* pixels, int width, int height,
                         int x, int y, int rw, int rh,
                         int rx, int ry2,
                         uint32_t color, const std::string& blend_mode, bool filled) {
    if (pixels == nullptr) return false;
    // Build SkPath-equivalent manually by using the existing RasterDrawPath if available
    // otherwise CPU rasterise
    PathData path;
    const float fx = static_cast<float>(x), fy = static_cast<float>(y);
    const float fw = static_cast<float>(rw), fh = static_cast<float>(rh);
    const float frx = static_cast<float>(rx), fry = static_cast<float>(ry2);
    path.push_back({PathVerb::kMove, {fx + frx, fy}});
    path.push_back({PathVerb::kLine, {fx + fw - frx, fy}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fx+fw, fy+fry}});
    path.push_back({PathVerb::kLine, {fx + fw, fy + fh - fry}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fx+fw-frx, fy+fh}});
    path.push_back({PathVerb::kLine, {fx + frx, fy + fh}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fx, fy+fh-fry}});
    path.push_back({PathVerb::kLine, {fx, fy + fry}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fx+frx, fy}});
    path.push_back({PathVerb::kClose, {}});
    return RasterDrawPath(pixels, width, height, path, color, blend_mode, filled);
}

bool RasterDrawEllipse(std::vector<uint32_t>* pixels, int width, int height,
                       int cx, int cy, int rxr, int ryr,
                       uint32_t color, const std::string& blend_mode, bool filled) {
    if (pixels == nullptr) return false;
    PathData path;
    const float fcx = static_cast<float>(cx), fcy = static_cast<float>(cy);
    const float frx = static_cast<float>(rxr), fry = static_cast<float>(ryr);
    path.push_back({PathVerb::kMove, {fcx + frx, fcy}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fcx, fcy + fry}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fcx - frx, fcy}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fcx, fcy - fry}});
    path.push_back({PathVerb::kArcTo, {frx, fry, 0, 0, 1, fcx + frx, fcy}});
    path.push_back({PathVerb::kClose, {}});
    return RasterDrawPath(pixels, width, height, path, color, blend_mode, filled);
}

bool RasterDrawArc(std::vector<uint32_t>* pixels, int width, int height,
                   int cx, int cy, int rxr, int ryr,
                   double start_deg, double sweep_deg,
                   uint32_t color, const std::string& blend_mode) {
    if (pixels == nullptr) return false;
    // Approximate arc as polyline segments
    const int segs = std::max(8, static_cast<int>(std::abs(sweep_deg) / 2.0));
    PathData path;
    for (int i = 0; i <= segs; ++i) {
        const double ang = (start_deg + sweep_deg * i / segs) * kPI / 180.0;
        const float px = static_cast<float>(cx + rxr * std::cos(ang));
        const float py = static_cast<float>(cy + ryr * std::sin(ang));
        if (i == 0) {
            path.push_back({PathVerb::kMove, {px, py}});
        } else {
            path.push_back({PathVerb::kLine, {px, py}});
        }
    }
    return RasterDrawPath(pixels, width, height, path, color, blend_mode, false);
}

bool RasterDrawThickLine(std::vector<uint32_t>* pixels, int width, int height,
                         float x0, float y0, float x1, float y1,
                         float stroke_width,
                         uint32_t color, const std::string& blend_mode) {
    if (pixels == nullptr) return false;
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx*dx + dy*dy);
    if (len < 1e-3f) return false;
    const float nx = -dy / len * stroke_width * 0.5f;
    const float ny =  dx / len * stroke_width * 0.5f;
    PathData path;
    path.push_back({PathVerb::kMove, {x0 + nx, y0 + ny}});
    path.push_back({PathVerb::kLine, {x1 + nx, y1 + ny}});
    path.push_back({PathVerb::kLine, {x1 - nx, y1 - ny}});
    path.push_back({PathVerb::kLine, {x0 - nx, y0 - ny}});
    path.push_back({PathVerb::kClose, {}});
    return RasterDrawPath(pixels, width, height, path, color, blend_mode, true);
}

bool RasterDropShadow(std::vector<uint32_t>* dst, int width, int height,
                      const std::vector<uint32_t>& src, int src_w, int src_h,
                      int offset_x, int offset_y,
                      double blur_sigma, uint32_t shadow_color) {
    if (dst == nullptr) return false;
    // Build shadow bitmap from src alpha
    std::vector<uint32_t> shadow(static_cast<size_t>(src_w * src_h), 0);
    const uint8_t sr = GetR_ps(shadow_color);
    const uint8_t sg = GetG_ps(shadow_color);
    const uint8_t sb = GetB_ps(shadow_color);
    for (size_t i = 0; i < src.size() && i < shadow.size(); ++i) {
        shadow[i] = PackRGBA_ps(sr, sg, sb, GetA_ps(src[i]));
    }
    FilterGaussianBlur(&shadow, src_w, src_h, blur_sigma, blur_sigma);
    // Composite shadow into dst
    ImageComposite(dst, width, height, shadow, src_w, src_h,
                   offset_x, offset_y, "srcOver", 1.0f);
    return true;
}

bool RasterGlow(std::vector<uint32_t>* pixels, int width, int height,
                double radius, uint32_t color, bool inner, float strength) {
    if (pixels == nullptr || radius <= 0) return false;
    const float sr = strength;
    // Build alpha mask
    std::vector<uint32_t> glow_buf = *pixels;
    const uint8_t gcr = GetR_ps(color);
    const uint8_t gcg = GetG_ps(color);
    const uint8_t gcb = GetB_ps(color);
    for (auto& px : glow_buf) {
        const uint8_t a = inner
            ? static_cast<uint8_t>(255 - GetA_ps(px))
            : GetA_ps(px);
        px = PackRGBA_ps(gcr, gcg, gcb, a);
    }
    FilterGaussianBlur(&glow_buf, width, height, radius, radius);
    for (size_t i = 0; i < pixels->size(); ++i) {
        uint32_t& dst = (*pixels)[i];
        const uint32_t gs = glow_buf[i];
        // Blend glow with strength
        const uint8_t a = static_cast<uint8_t>(GetA_ps(gs) * sr);
        const uint32_t gs_modulated = (gs & 0xFFFFFF00u) | a;
        dst = static_cast<uint32_t>(BlendModeApply("screen", dst, gs_modulated));
    }
    return true;
}

// ===========================================================================
// Text rendering
// ===========================================================================

bool RasterDrawText(std::vector<uint32_t>* pixels, int width, int height,
                    const std::string& text, float x, float y,
                    const FontOptions& font,
                    uint32_t color, const std::string& blend_mode) {
#if PS_HAS_SKIA
    if (pixels == nullptr || text.empty()) return false;
    // Build SkBitmap from pixels (local implementation mirrors bridge)
    SkImageInfo info = SkImageInfo::Make(width, height,
                                        kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    SkBitmap bmp;
    if (!bmp.tryAllocPixels(info)) return false;
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const uint32_t px = (*pixels)[static_cast<size_t>(row) * width + col];
            bmp.erase(SkColorSetARGB(GetA_ps(px), GetR_ps(px), GetG_ps(px), GetB_ps(px)),
                      {col, row, col+1, row+1});
        }
    }
    SkCanvas canvas(bmp);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SkColorSetARGB(GetA_ps(color), GetR_ps(color), GetG_ps(color), GetB_ps(color)));

    SkFont sk_font;
    sk_font.setSize(static_cast<SkScalar>(font.size));
    if (font.weight >= 700) sk_font.setEmbolden(true);

    canvas.drawSimpleText(text.c_str(), text.size(),
                          SkTextEncoding::kUTF8,
                          static_cast<SkScalar>(x), static_cast<SkScalar>(y),
                          sk_font, paint);

    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            const SkColor sc = bmp.getColor(col, row);
            (*pixels)[static_cast<size_t>(row) * width + col] =
                PackRGBA_ps(SkColorGetR(sc), SkColorGetG(sc),
                             SkColorGetB(sc), SkColorGetA(sc));
        }
    }
    return true;
#else
    // Minimal fallback: no native Skia text, return false
    (void)pixels; (void)width; (void)height; (void)text;
    (void)x; (void)y; (void)font; (void)color; (void)blend_mode;
    return false;
#endif
}

bool MeasureText(const std::string& text, const FontOptions& font,
                 float* out_width, float* out_height, float* out_baseline) {
#if PS_HAS_SKIA
    if (out_width == nullptr) return false;
    SkFont sk_font;
    sk_font.setSize(static_cast<SkScalar>(font.size));
    SkRect bounds;
    (void)sk_font.measureText(text.c_str(), text.size(),
                               SkTextEncoding::kUTF8, &bounds);
    if (out_width)    *out_width    = bounds.width();
    if (out_height)   *out_height   = bounds.height();
    if (out_baseline) *out_baseline = -bounds.fTop;
    return true;
#else
    // Approximate
    const float char_w = static_cast<float>(font.size) * 0.6f;
    if (out_width)    *out_width    = char_w * static_cast<float>(text.size());
    if (out_height)   *out_height   = static_cast<float>(font.size) * 1.2f;
    if (out_baseline) *out_baseline = static_cast<float>(font.size);
    return true;
#endif
}

// ===========================================================================
// Mask / Selection
// ===========================================================================

bool ApplyMask(std::vector<uint32_t>* pixels, int width, int height,
               const std::vector<uint8_t>& mask_grey) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    const size_t n = pixels->size();
    for (size_t i = 0; i < n && i < mask_grey.size(); ++i) {
        uint32_t& px = (*pixels)[i];
        const float ma = mask_grey[i] / 255.f;
        px = (px & 0xFFFFFF00u) | static_cast<uint8_t>(GetA_ps(px) * ma);
    }
    return true;
}

bool MaskErode(std::vector<uint8_t>* mask, int width, int height, int radius) {
    if (mask == nullptr || radius < 1) return false;
    const std::vector<uint8_t> src = *mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t mn = 255;
            for (int ky = -radius; ky <= radius; ++ky) {
                for (int kx = -radius; kx <= radius; ++kx) {
                    const int sx = std::max(0, std::min(width  - 1, x + kx));
                    const int sy = std::max(0, std::min(height - 1, y + ky));
                    mn = std::min(mn, src[static_cast<size_t>(sy) * width + sx]);
                }
            }
            (*mask)[static_cast<size_t>(y) * width + x] = mn;
        }
    }
    return true;
}

bool MaskDilate(std::vector<uint8_t>* mask, int width, int height, int radius) {
    if (mask == nullptr || radius < 1) return false;
    const std::vector<uint8_t> src = *mask;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            uint8_t mx = 0;
            for (int ky = -radius; ky <= radius; ++ky) {
                for (int kx = -radius; kx <= radius; ++kx) {
                    const int sx = std::max(0, std::min(width  - 1, x + kx));
                    const int sy = std::max(0, std::min(height - 1, y + ky));
                    mx = std::max(mx, src[static_cast<size_t>(sy) * width + sx]);
                }
            }
            (*mask)[static_cast<size_t>(y) * width + x] = mx;
        }
    }
    return true;
}

bool MaskFromColorRange(const std::vector<uint32_t>& pixels, int width, int height,
                        uint32_t seed_color, int tolerance,
                        std::vector<uint8_t>* out_mask) {
    if (out_mask == nullptr) return false;
    (void)width; (void)height;
    const size_t n = pixels.size();
    out_mask->resize(n);
    for (size_t i = 0; i < n; ++i) {
        const int dist = ColorDist(pixels[i], seed_color);
        (*out_mask)[i] = static_cast<uint8_t>(
            255 - std::min(255, dist * 255 / std::max(1, tolerance)));
    }
    return true;
}

// ===========================================================================
// Histogram
// ===========================================================================

bool ComputeHistogram(const std::vector<uint32_t>& pixels, int width, int height,
                      Histogram* out) {
    if (out == nullptr) return false;
    (void)width; (void)height;
    out->r.assign(256, 0); out->g.assign(256, 0);
    out->b.assign(256, 0); out->a.assign(256, 0);
    out->luma.assign(256, 0);
    for (const auto& px : pixels) {
        out->r[GetR_ps(px)]++;
        out->g[GetG_ps(px)]++;
        out->b[GetB_ps(px)]++;
        out->a[GetA_ps(px)]++;
        const int l = static_cast<int>(Luminance(
            GetR_ps(px)/255.f, GetG_ps(px)/255.f, GetB_ps(px)/255.f) * 255.f);
        out->luma[static_cast<size_t>(std::max(0, std::min(255, l)))]++;
    }
    return true;
}

bool AutoLevels(std::vector<uint32_t>* pixels, int width, int height,
                double clip_percent) {
    if (pixels == nullptr) return false;
    Histogram h;
    ComputeHistogram(*pixels, width, height, &h);
    const size_t total = pixels->size();
    const size_t clip = static_cast<size_t>(total * clip_percent / 100.0);
    auto find_level = [&](const std::vector<uint32_t>& hist, bool low) -> int {
        size_t cum = 0;
        if (low) {
            for (int i = 0; i < 256; ++i) {
                cum += hist[static_cast<size_t>(i)];
                if (cum >= clip) return i;
            }
            return 0;
        } else {
            for (int i = 255; i >= 0; --i) {
                cum += hist[static_cast<size_t>(i)];
                if (cum >= clip) return i;
            }
            return 255;
        }
    };
    const int rl = find_level(h.r, true),  rh = find_level(h.r, false);
    const int gl = find_level(h.g, true),  gh = find_level(h.g, false);
    const int bl = find_level(h.b, true),  bh = find_level(h.b, false);
    for (auto& px : *pixels) {
        auto lv = [](int v, int lo, int hi) -> uint8_t {
            if (hi <= lo) return static_cast<uint8_t>(v);
            return Clamp8(static_cast<int>((v - lo) * 255.0 / (hi - lo) + 0.5));
        };
        px = PackRGBA_ps(lv(GetR_ps(px), rl, rh),
                          lv(GetG_ps(px), gl, gh),
                          lv(GetB_ps(px), bl, bh),
                          GetA_ps(px));
    }
    return true;
}

bool EqualizeHistogram(std::vector<uint32_t>* pixels, int width, int height) {
    if (pixels == nullptr) return false;
    (void)width; (void)height;
    Histogram h;
    ComputeHistogram(*pixels, width, height, &h);
    const float n = static_cast<float>(pixels->size());
    // Build CDF-based LUTs for each channel
    auto build_lut = [&](const std::vector<uint32_t>& hist) {
        std::array<uint8_t, 256> lut{};
        float cdf = 0.f, cdf_min = 0.f;
        bool found_min = false;
        for (int i = 0; i < 256; ++i) {
            cdf += hist[static_cast<size_t>(i)];
            if (!found_min && hist[static_cast<size_t>(i)] > 0) { cdf_min = cdf; found_min = true; }
            lut[static_cast<size_t>(i)] = (n > cdf_min)
                ? Clamp8f((cdf - cdf_min) / (n - cdf_min) * 255.f)
                : static_cast<uint8_t>(i);
        }
        return lut;
    };
    const auto lr = build_lut(h.r);
    const auto lg = build_lut(h.g);
    const auto lb = build_lut(h.b);
    for (auto& px : *pixels) {
        px = PackRGBA_ps(lr[GetR_ps(px)], lg[GetG_ps(px)],
                          lb[GetB_ps(px)], GetA_ps(px));
    }
    return true;
}

}  // namespace XER::Bridge::Skia
