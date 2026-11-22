// builtin_fx.cc — host-side implementations of the retired
// xer_fx_* / xer_audio_fx plugin kernels. Each function keeps the
// plugin ABI signature (sdk/xer_plugin.h) and the original positional
// parameter defaults/clamps so serialized "plugin:<id>" ClipEffects
// behave exactly as they did under the DLLs.
//
// Routing: effects with a semantic equivalent in the Skia bridge
// (wrapper/skia) or XER_AudioCore effects_rack delegate there —
// BGRA8 frames are repacked to the bridge's 0xRRGGBBAA uint32 layout
// and back (same conversion as src/video/video_effects.cc). Kernels
// with no equivalent are ported verbatim:
//   * radial_blur        — cx/cy center params the bridge API lacks
//   * duotone/invert_lum/color_isolate — luma/hue-driven, no LUT form
//   * wave/ripple/mirror/tile/twirl — geometric remaps; src/image only
//     offers a generic DisplacementMap needing a synthesized field
//   * letterbox/grid/border/corner — draw ops, no bridge equivalent
//   * vignette_soft — bridge vignette cannot express partial-depth
//     darkening + radius semantics
//   * scanlines/neon_edges/halftone/emboss_color/edge_glow/tone_split
//   * echo/tremolo/autopan/bitcrush/onepole — effects_rack versions
//     carry different transfer curves (ModTremolo gain shape,
//     DistortionBitcrush quantizer, CircularDelayLine can't express
//     feedback echo) or none at all
#include "builtin_fx.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "audio/effects_rack/fx_distortion_tube.h"
#include "voice_isolate.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::BuiltinFx {
namespace {

namespace Skia = XER::Bridge::Skia;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.283185307179586f;

float Param(const float* p, int32_t n, int32_t i, float d) {
    return (p != nullptr && i < n) ? p[i] : d;
}

bool VideoValid(const XER_VideoFrame* f) {
    return f != nullptr && f->data != nullptr && f->width > 0 &&
           f->height > 0 && f->stride >= f->width * 4;
}

bool AudioValid(const XER_AudioBuffer* b) {
    return b != nullptr && b->samples != nullptr && b->frames > 0 &&
           b->channels > 0 && b->sample_rate > 0;
}

uint8_t Clamp8f(float v) {
    return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f));
}

// ── BGRA8 (strided) ↔ Skia bridge uint32 (0xRRGGBBAA, packed) ───────

std::vector<uint32_t> PackSkia(const XER_VideoFrame& f) {
    std::vector<uint32_t> px(static_cast<size_t>(f.width) * f.height);
    for (int32_t y = 0; y < f.height; ++y) {
        const uint8_t* row =
            f.data + static_cast<size_t>(y) * f.stride;
        for (int32_t x = 0; x < f.width; ++x) {
            const uint8_t* p = row + static_cast<size_t>(x) * 4;
            px[static_cast<size_t>(y) * f.width + x] =
                Skia::MakeColorRGBA(p[2], p[1], p[0], p[3]);
        }
    }
    return px;
}

void UnpackSkia(const std::vector<uint32_t>& px, XER_VideoFrame* f) {
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            const uint32_t c = px[static_cast<size_t>(y) * f->width + x];
            uint8_t* p = row + static_cast<size_t>(x) * 4;
            p[2] = static_cast<uint8_t>((c >> 24) & 0xFF);
            p[1] = static_cast<uint8_t>((c >> 16) & 0xFF);
            p[0] = static_cast<uint8_t>((c >> 8) & 0xFF);
            p[3] = static_cast<uint8_t>(c & 0xFF);
        }
    }
}

// Runs a Skia bridge op on the frame: pack → fn → unpack. `fn`
// receives (pixels*, w, h) and returns the bridge bool.
template <typename Fn>
int ApplySkia(XER_VideoFrame* f, Fn&& fn) {
    if (!VideoValid(f)) return 1;
    std::vector<uint32_t> px = PackSkia(*f);
    if (!fn(&px, f->width, f->height)) return 1;
    UnpackSkia(px, f);
    return 0;
}

// ── adjust ──────────────────────────────────────────────────────────
// p0 stops -4..4 → Skia AdjustExposure (same 2^stops scale).
int exposure(XER_VideoFrame* f, const float* p, int32_t n) {
    const double stops =
        std::clamp<double>(Param(p, n, 0, 0.f), -4.0, 4.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustExposure(px, w, h, stops, 1.0);
    });
}

// p0 gamma 0.2..4 → AdjustExposure(exposure=0, gamma).
int gamma_corr(XER_VideoFrame* f, const float* p, int32_t n) {
    const double gamma =
        std::clamp<double>(Param(p, n, 0, 1.f), 0.2, 4.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustExposure(px, w, h, 0.0, gamma);
    });
}

// p0 black 0..128, p1 white 128..255, p2 mid gamma 0.2..4 → AdjustLevels.
int levels(XER_VideoFrame* f, const float* p, int32_t n) {
    const int black =
        static_cast<int>(std::clamp(Param(p, n, 0, 0.f), 0.f, 128.f));
    const int white =
        static_cast<int>(std::clamp(Param(p, n, 1, 255.f), 128.f, 255.f));
    const double gamma =
        std::clamp<double>(Param(p, n, 2, 1.f), 0.2, 4.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustLevels(px, w, h, black, white, gamma, 0, 255);
    });
}

// p0 clip% 0..10 → AutoLevels per-channel histogram clipping.
int auto_contrast(XER_VideoFrame* f, const float* p, int32_t n) {
    const double clip =
        std::clamp<double>(Param(p, n, 0, 0.5f), 0.0, 10.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AutoLevels(px, w, h, clip);
    });
}

// p0 -100..100 → R += d, B -= d via the channel mixer's constant term
// (d * 0.6, as the DLL kernel scaled it).
int temperature(XER_VideoFrame* f, const float* p, int32_t n) {
    const double d =
        std::clamp<double>(Param(p, n, 0, 0.f), -100.0, 100.0) * 0.6;
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustChannelMixer(
            px, w, h, {1.0, 0.0, 0.0, d / 255.0}, {0.0, 1.0, 0.0, 0.0},
            {0.0, 0.0, 1.0, -d / 255.0});
    });
}

// p0 -100..100 → G -= d, R/B += d (d * 0.5).
int tint(XER_VideoFrame* f, const float* p, int32_t n) {
    const double d =
        std::clamp<double>(Param(p, n, 0, 0.f), -100.0, 100.0) * 0.5;
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustChannelMixer(
            px, w, h, {1.0, 0.0, 0.0, d / 255.0},
            {0.0, 1.0, 0.0, -d / 255.0}, {0.0, 0.0, 1.0, d / 255.0});
    });
}

// p0 -100..100 → AdjustVibrance (host equivalent; skin-tone protected
// curve replaces the DLL's luma-relative scale, same param range).
int vibrance(XER_VideoFrame* f, const float* p, int32_t n) {
    const double amt =
        std::clamp<double>(Param(p, n, 0, 0.f), -100.0, 100.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustVibrance(px, w, h, amt);
    });
}

// p0 lift -1..1, p1 gamma 0.2..4, p2 gain 0..4 — the DLL formula is a
// per-channel LUT, so route through AdjustCurves with the same LUT.
int lift_gamma_gain(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float lift = std::clamp(Param(p, n, 0, 0.f), -1.f, 1.f);
    const float gamma = std::clamp(Param(p, n, 1, 1.f), 0.2f, 4.f);
    const float gain = std::clamp(Param(p, n, 2, 1.f), 0.f, 4.f);
    const float inv = 1.f / gamma;
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        lut[static_cast<size_t>(i)] = Clamp8f(
            std::pow(std::max(i / 255.f + lift, 0.f), inv) * gain *
            255.f);
    }
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustCurves(px, w, h, lut.data(), lut.data(),
                                  lut.data(), nullptr);
    });
}

// ── blur ────────────────────────────────────────────────────────────
// p0 radius px 1..64, p1 angle deg → FilterMotionBlur. The bridge
// kernel samples forward along the ray (one-sided); the DLL averaged
// ±r — same directional-smear effect, sample count preserved.
int directional_blur(XER_VideoFrame* f, const float* p, int32_t n) {
    const int r = std::clamp(
        static_cast<int>(std::lround(Param(p, n, 0, 12.f))), 1, 64);
    const double angle = Param(p, n, 1, 0.f);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::FilterMotionBlur(px, w, h, angle, 2 * r + 1);
    });
}

// p0 strength 0..1, p1 cx 0..1, p2 cy 0..1 — ported: the bridge's
// radial blur is frame-center-locked and can't express cx/cy.
int radial_blur(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    constexpr int32_t kSamples = 16;
    const float s = std::clamp(Param(p, n, 0, 0.3f), 0.f, 1.f);
    const float cx =
        std::clamp(Param(p, n, 1, 0.5f), 0.f, 1.f) * (f->width - 1);
    const float cy =
        std::clamp(Param(p, n, 2, 0.5f), 0.f, 1.f) * (f->height - 1);
    const int32_t w = f->width, h = f->height, st = f->stride;
    const std::vector<uint8_t> src(f->data,
                                   f->data + static_cast<size_t>(h) * st);
    const auto at = [&](float x, float y) -> const uint8_t* {
        const int32_t ix = std::clamp(
            static_cast<int32_t>(std::lround(x)), 0, w - 1);
        const int32_t iy = std::clamp(
            static_cast<int32_t>(std::lround(y)), 0, h - 1);
        return src.data() + static_cast<size_t>(iy) * st +
               static_cast<size_t>(ix) * 4;
    };
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int32_t x = 0; x < w; ++x) {
            float b = 0, g = 0, rr = 0, a = 0, wsum = 0;
            for (int32_t i = 0; i < kSamples; ++i) {
                const float t = static_cast<float>(i) / (kSamples - 1.f);
                const float wgt = static_cast<float>(kSamples - i);
                const uint8_t* q = at(x + (cx - x) * t * s,
                                      y + (cy - y) * t * s);
                b += q[0] * wgt;
                g += q[1] * wgt;
                rr += q[2] * wgt;
                a += q[3] * wgt;
                wsum += wgt;
            }
            uint8_t* d = row + static_cast<size_t>(x) * 4;
            const float inv = 1.f / wsum;
            d[0] = static_cast<uint8_t>(b * inv);
            d[1] = static_cast<uint8_t>(g * inv);
            d[2] = static_cast<uint8_t>(rr * inv);
            d[3] = static_cast<uint8_t>(a * inv);
        }
    }
    return 0;
}

// p0 strength 0..1 → FilterRadialBlur zoom mode (frame-center zoom,
// like the DLL). The bridge pull fraction is amount/(100+amount), so
// amount = 100*s/(1-s) maps s → the same max displacement (capped —
// s=1 is asymptotic).
int zoom_blur(XER_VideoFrame* f, const float* p, int32_t n) {
    const float s = std::clamp(Param(p, n, 0, 0.3f), 0.f, 1.f);
    if (s <= 0.f) return 0;   // zero-strength identity
    const double amount =
        std::min(900.0, 100.0 * static_cast<double>(s) / (1.0 - s));
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::FilterRadialBlur(px, w, h, amount, true);
    });
}

// p0 radius px 1..32 → FilterGaussianBlur. The DLL was 3 box passes of
// radius r ≈ gaussian with sigma = sqrt(r(r+1)) (box-variance match).
int fast_gaussian(XER_VideoFrame* f, const float* p, int32_t n) {
    const int r = std::clamp(
        static_cast<int>(std::lround(Param(p, n, 0, 6.f))), 1, 32);
    const double sigma = std::sqrt(static_cast<double>(r) * (r + 1));
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::FilterGaussianBlur(px, w, h, sigma, sigma);
    });
}

// p0 amount 0..4, p1 radius 1..16 → FilterSharpen (unsharp mask);
// sigma matched to the DLL's box-blur variance.
int unsharp_mask(XER_VideoFrame* f, const float* p, int32_t n) {
    const double amt =
        std::clamp<double>(Param(p, n, 0, 1.f), 0.0, 4.0);
    const int r = std::clamp(
        static_cast<int>(std::lround(Param(p, n, 1, 3.f))), 1, 16);
    const double sigma =
        std::sqrt(static_cast<double>(r) * (r + 1) / 3.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::FilterSharpen(px, w, h, amt, sigma, 0);
    });
}

// p0 passes 1..4 → FilterMedian(radius=1) applied per pass.
int median3(XER_VideoFrame* f, const float* p, int32_t n) {
    const int passes = std::clamp(
        static_cast<int>(std::lround(Param(p, n, 0, 1.f))), 1, 4);
    if (passes <= 0) return 0;
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        for (int i = 0; i < passes; ++i) {
            if (!Skia::FilterMedian(px, w, h, 1)) return false;
        }
        return true;
    });
}

// ── color ───────────────────────────────────────────────────────────
// p0 degrees → AdjustHSL hue rotation.
int hue_rotate(XER_VideoFrame* f, const float* p, int32_t n) {
    const double deg = Param(p, n, 0, 0.f);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustHSL(px, w, h, deg, 0.0, 0.0);
    });
}

// p0 amount 0..1 → AdjustSepia (identical coefficients + blend).
int sepia_tone(XER_VideoFrame* f, const float* p, int32_t n) {
    const double t = std::clamp<double>(Param(p, n, 0, 1.f), 0.0, 1.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustSepia(px, w, h, t);
    });
}

// p0 shadow hue, p1 light hue — ported (luma-driven ramp; no LUT form).
int duotone(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const auto hue_to_rgb = [](float deg, float* r, float* g,
                               float* b) {
        float h = std::fmod(deg, 360.0f);
        if (h < 0.0f) h += 360.0f;
        const float x =
            1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f);
        if (h < 60.0f)       { *r = 1.f; *g = x;   *b = 0.f; }
        else if (h < 120.0f) { *r = x;   *g = 1.f; *b = 0.f; }
        else if (h < 180.0f) { *r = 0.f; *g = 1.f; *b = x;   }
        else if (h < 240.0f) { *r = 0.f; *g = x;   *b = 1.f; }
        else if (h < 300.0f) { *r = x;   *g = 0.f; *b = 1.f; }
        else                 { *r = 1.f; *g = 0.f; *b = x;   }
    };
    float sr, sg, sb, hr, hg, hb;
    hue_to_rgb(Param(p, n, 0, 220.f), &sr, &sg, &sb);
    hue_to_rgb(Param(p, n, 1, 40.f), &hr, &hg, &hb);
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, row += 4) {
            const float b = row[0], g = row[1], r = row[2];
            const float l =
                (0.299f * r + 0.587f * g + 0.114f * b) * (1.f / 255.f);
            row[2] = Clamp8f((sr + (hr - sr) * l) * 255.f + 0.5f);
            row[1] = Clamp8f((sg + (hg - sg) * l) * 255.f + 0.5f);
            row[0] = Clamp8f((sb + (hb - sb) * l) * 255.f + 0.5f);
        }
    }
    return 0;
}

// p0..p2 per-channel gains 0..2 → AdjustChannelMixer diagonal.
int channel_mixer(XER_VideoFrame* f, const float* p, int32_t n) {
    const double gr =
        std::clamp<double>(Param(p, n, 0, 1.f), 0.0, 2.0);
    const double gg =
        std::clamp<double>(Param(p, n, 1, 1.f), 0.0, 2.0);
    const double gb =
        std::clamp<double>(Param(p, n, 2, 1.f), 0.0, 2.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustChannelMixer(px, w, h, {gr, 0.0, 0.0, 0.0},
                                        {0.0, gg, 0.0, 0.0},
                                        {0.0, 0.0, gb, 0.0});
    });
}

// p0 amount 0..1 — ported (per-pixel luma shift).
int invert_lum(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float amt = std::clamp(Param(p, n, 0, 1.f), 0.f, 1.f);
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const float b = px[0], g = px[1], r = px[2];
            const float luma = 0.114f * b + 0.587f * g + 0.299f * r;
            const float shift = amt * (255.f - 2.f * luma);
            px[0] = Clamp8f(b + shift + 0.5f);
            px[1] = Clamp8f(g + shift + 0.5f);
            px[2] = Clamp8f(r + shift + 0.5f);
        }
    }
    return 0;
}

// p0 target hue deg, p1 tolerance deg 1..90 — ported (hue-keyed
// desaturation with feather).
int color_isolate(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float target = Param(p, n, 0, 0.f);
    const float tol = std::clamp(Param(p, n, 1, 20.f), 1.f, 90.f);
    const float feather = std::min(20.f, tol * 0.5f);
    const auto hue_of = [](float b, float g, float r) {
        const float mx = std::max({r, g, b});
        const float mn = std::min({r, g, b});
        const float d = mx - mn;
        if (d <= 0.f) return 0.f;
        float h;
        if (mx == r)       h = 60.f * std::fmod((g - b) / d, 6.f);
        else if (mx == g)  h = 60.f * ((b - r) / d + 2.f);
        else               h = 60.f * ((r - g) / d + 4.f);
        if (h < 0.f) h += 360.f;
        return h;
    };
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* px = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            const float b = px[0], g = px[1], r = px[2];
            float d = std::fabs(hue_of(b, g, r) - target);
            if (d > 180.f) d = 360.f - d;
            const float k =
                std::clamp((tol + feather - d) / feather, 0.f, 1.f);
            if (k >= 1.f) continue;
            const float ly = 0.114f * b + 0.587f * g + 0.299f * r;
            px[0] = Clamp8f(ly + (b - ly) * k + 0.5f);
            px[1] = Clamp8f(ly + (g - ly) * k + 0.5f);
            px[2] = Clamp8f(ly + (r - ly) * k + 0.5f);
        }
    }
    return 0;
}

// p0 channel order 0..5 → AdjustChannelMixer permutation matrix.
int rgb_swap(XER_VideoFrame* f, const float* p, int32_t n) {
    static constexpr int kPerm[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    const int ord = std::clamp(
        static_cast<int>(Param(p, n, 0, 0.f) + 0.5f), 0, 5);
    if (ord == 0) return 0;   // identity permutation
    const int* pm = kPerm[ord];
    // pm[oc] selects the source channel (0=B,1=G,2=R) for output
    // channel oc in memory order; bridge coeffs are named per RGB.
    Skia::ChannelMixerCoeff out[3] = {};
    for (int oc = 0; oc < 3; ++oc) {
        double* coeff = pm[oc] == 0 ? &out[oc].b
                        : pm[oc] == 1 ? &out[oc].g
                                      : &out[oc].r;
        *coeff = 1.0;
    }
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        // Bridge takes (out_r, out_g, out_b) — out[] is memory order
        // B,G,R here, so pass reversed.
        return Skia::AdjustChannelMixer(px, w, h, out[2], out[1],
                                        out[0]);
    });
}

// p0 -100..100 → AdjustHSL saturation (param range/semantics match;
// the bridge adjusts in HSL space vs the DLL's luma-relative scale).
int saturation_boost(XER_VideoFrame* f, const float* p, int32_t n) {
    const double amt =
        std::clamp<double>(Param(p, n, 0, 30.f), -100.0, 100.0);
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustHSL(px, w, h, 0.0, amt, 0.0);
    });
}

// ── distort — ported geometric remaps (nearest-neighbour, edge
// clamped; src/image only provides a generic displacement-map engine,
// so the dedicated kernels stay). ────────────────────────────────────

std::vector<uint8_t> Snapshot(const XER_VideoFrame* f) {
    return {f->data,
            f->data + static_cast<size_t>(f->stride) * f->height};
}

int wave_h(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float amp =
        std::clamp(Param(p, n, 0, 12.f), 0.f, 64.f);
    const float wl =
        std::clamp(Param(p, n, 1, 120.f), 8.f, 512.f);
    const int32_t w = f->width, h = f->height;
    const std::vector<uint8_t> src = Snapshot(f);
    for (int32_t y = 0; y < h; ++y) {
        const int32_t shift = static_cast<int32_t>(
            std::lround(amp * std::sin(kTwoPi * y / wl)));
        uint8_t* dst = f->data + static_cast<size_t>(y) * f->stride;
        const uint8_t* srow =
            src.data() + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x) {
            std::copy_n(srow +
                            static_cast<size_t>(
                                std::clamp(x - shift, 0, w - 1)) * 4,
                        4, dst + static_cast<size_t>(x) * 4);
        }
    }
    return 0;
}

int wave_v(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float amp =
        std::clamp(Param(p, n, 0, 12.f), 0.f, 64.f);
    const float wl =
        std::clamp(Param(p, n, 1, 120.f), 8.f, 512.f);
    const int32_t w = f->width, h = f->height;
    const std::vector<uint8_t> src = Snapshot(f);
    for (int32_t x = 0; x < w; ++x) {
        const int32_t shift = static_cast<int32_t>(
            std::lround(amp * std::sin(kTwoPi * x / wl)));
        for (int32_t y = 0; y < h; ++y) {
            const int32_t sy = std::clamp(y - shift, 0, h - 1);
            std::copy_n(src.data() +
                            static_cast<size_t>(sy) * f->stride +
                            static_cast<size_t>(x) * 4,
                        4,
                        f->data + static_cast<size_t>(y) * f->stride +
                            static_cast<size_t>(x) * 4);
        }
    }
    return 0;
}

int ripple(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float amp = std::clamp(Param(p, n, 0, 10.f), 0.f, 32.f);
    const float wl = std::clamp(Param(p, n, 1, 60.f), 8.f, 256.f);
    const int32_t w = f->width, h = f->height;
    const std::vector<uint8_t> src = Snapshot(f);
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float max_dist = std::max(std::hypot(cx, cy), 1e-6f);
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* dst = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x) {
            const float dx = x - cx, dy = y - cy;
            const float dist = std::hypot(dx, dy);
            int32_t sx = x, sy = y;
            if (dist > 1e-6f) {
                const float d =
                    amp * std::sin(kTwoPi * dist / wl) *
                    std::max(0.f, 1.f - dist / max_dist);
                sx = static_cast<int32_t>(
                    std::lround(x - dx / dist * d));
                sy = static_cast<int32_t>(
                    std::lround(y - dy / dist * d));
            }
            std::copy_n(src.data() +
                            static_cast<size_t>(
                                std::clamp(sy, 0, h - 1)) * f->stride +
                            static_cast<size_t>(
                                std::clamp(sx, 0, w - 1)) * 4,
                        4, dst + static_cast<size_t>(x) * 4);
        }
    }
    return 0;
}

int mirror_quad(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int w = f->width, h = f->height;
    const int mode =
        std::clamp(static_cast<int>(Param(p, n, 0, 0.f)), 0, 3);
    const int hw = w / 2, hh = h / 2;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int x = 0; x < w; ++x) {
            int sx = x, sy = y;
            switch (mode) {
                case 0: if (x >= hw) sx = w - 1 - x; break;
                case 1: if (x < hw)  sx = w - 1 - x; break;
                case 2: if (y >= hh) sy = h - 1 - y; break;
                case 3: if (y < hh)  sy = h - 1 - y; break;
            }
            const uint8_t* s =
                f->data + static_cast<size_t>(sy) * f->stride +
                static_cast<size_t>(sx) * 4;
            uint8_t* d = row + static_cast<size_t>(x) * 4;
            if (s != d) std::copy_n(s, 4, d);
        }
    }
    return 0;
}

int tile(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int w = f->width, h = f->height;
    const int count =
        std::clamp(static_cast<int>(Param(p, n, 0, 3.f)), 2, 8);
    const std::vector<uint8_t> src = Snapshot(f);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        const uint8_t* srow =
            src.data() +
            static_cast<size_t>((y * count) % h) * f->stride;
        for (int x = 0; x < w; ++x) {
            std::copy_n(srow + static_cast<size_t>((x * count) % w) * 4,
                        4, row + static_cast<size_t>(x) * 4);
        }
    }
    return 0;
}

int twirl(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int w = f->width, h = f->height;
    const float deg =
        std::clamp(Param(p, n, 0, 90.f), -360.f, 360.f);
    if (deg == 0.f) return 0;
    const float ang = deg * (kPi / 180.f);
    const float cx = (w - 1) * 0.5f, cy = (h - 1) * 0.5f;
    const float max_d = std::sqrt(cx * cx + cy * cy);
    if (max_d <= 0.f) return 0;
    const std::vector<uint8_t> src = Snapshot(f);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int x = 0; x < w; ++x) {
            const float dx = x - cx, dy = y - cy;
            const float d = std::sqrt(dx * dx + dy * dy);
            float sx = static_cast<float>(x), sy = static_cast<float>(y);
            if (d < max_d) {
                const float t = ang * (1.f - d / max_d);
                const float c = std::cos(t), s = std::sin(t);
                sx = cx + dx * c - dy * s;
                sy = cy + dx * s + dy * c;
            }
            const int ix = std::clamp(
                static_cast<int>(std::lround(sx)), 0, w - 1);
            const int iy = std::clamp(
                static_cast<int>(std::lround(sy)), 0, h - 1);
            std::copy_n(src.data() +
                            static_cast<size_t>(iy) * f->stride +
                            static_cast<size_t>(ix) * 4,
                        4, row + static_cast<size_t>(x) * 4);
        }
    }
    return 0;
}

// ── overlay ─────────────────────────────────────────────────────────
// p0 amount 0..100, p1 monochrome 0/1 → FilterAddNoise uniform
// (DLL scale was ±amount*0.5).
int film_grain(XER_VideoFrame* f, const float* p, int32_t n) {
    const int amount = static_cast<int>(std::lround(
        std::clamp(Param(p, n, 0, 20.f), 0.f, 100.f) * 0.5f));
    const bool mono = Param(p, n, 1, 1.f) >= 0.5f;
    if (amount <= 0) return 0;
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::FilterAddNoise(px, w, h, amount,
                                    /*gaussian=*/false, mono);
    });
}

// p0 bar fraction 0..0.45 — ported (draw op).
int letterbox(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float frac =
        std::clamp(Param(p, n, 0, 0.12f), 0.f, 0.45f);
    const int32_t bar =
        static_cast<int32_t>(std::lround(frac * f->height));
    for (int32_t y = 0; y < f->height; ++y) {
        if (y >= bar && y < f->height - bar) continue;
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            px[0] = px[1] = px[2] = 0;
            px[3] = 255;
        }
    }
    return 0;
}

// p0 strength 0..1, p1 radius 0..1 — ported (the bridge vignette only
// reaches full black; it can't express the DLL's partial-depth
// smoothstep from `radius` inward).
int vignette_soft(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float strength = std::clamp(Param(p, n, 0, 0.5f), 0.f, 1.f);
    const float radius = std::clamp(Param(p, n, 1, 0.7f), 0.f, 1.f);
    const float cx = f->width * 0.5f;
    const float cy = f->height * 0.5f;
    const float max_d = std::sqrt(cx * cx + cy * cy);
    if (max_d <= 0.f) return 0;
    const auto smoothstep = [](float e0, float e1, float x) {
        const float d = e1 - e0;
        const float t = d != 0.f
                            ? std::clamp((x - e0) / d, 0.f, 1.f)
                            : (x < e0 ? 0.f : 1.f);
        return t * t * (3.f - 2.f * t);
    };
    for (int32_t y = 0; y < f->height; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        const float dy = (y + 0.5f) - cy;
        for (int32_t x = 0; x < f->width; ++x) {
            const float dx = (x + 0.5f) - cx;
            const float nd = std::sqrt(dx * dx + dy * dy) / max_d;
            const float factor =
                1.f - strength * smoothstep(radius, 1.f, nd);
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            for (int c = 0; c < 3; ++c) {
                px[c] = Clamp8f(px[c] * factor);
            }
        }
    }
    return 0;
}

// Grid/border/corner draw ops — ported.
void BlendTo(uint8_t* px, uint8_t v, float a) {
    px[0] = static_cast<uint8_t>(std::lround(px[0] + (v - px[0]) * a));
    px[1] = static_cast<uint8_t>(std::lround(px[1] + (v - px[1]) * a));
    px[2] = static_cast<uint8_t>(std::lround(px[2] + (v - px[2]) * a));
}

void FillRect(XER_VideoFrame* f, int32_t x, int32_t y, int32_t w,
              int32_t h, uint8_t v, float a) {
    const int32_t x0 = std::max(x, 0), y0 = std::max(y, 0);
    const int32_t x1 = std::min(x + w, f->width),
                  y1 = std::min(y + h, f->height);
    for (int32_t yy = y0; yy < y1; ++yy) {
        uint8_t* px = f->data + static_cast<size_t>(yy) * f->stride +
                      static_cast<size_t>(x0) * 4;
        for (int32_t xx = x0; xx < x1; ++xx, px += 4) BlendTo(px, v, a);
    }
}

int grid_lines(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int32_t cells = static_cast<int32_t>(
        std::clamp(Param(p, n, 0, 4.f), 2.f, 16.f));
    const float a = std::clamp(Param(p, n, 1, 0.3f), 0.f, 1.f);
    for (int32_t i = 1; i < cells; ++i) {
        FillRect(f, i * f->width / cells, 0, 1, f->height, 255, a);
        FillRect(f, 0, i * f->height / cells, f->width, 1, 255, a);
    }
    return 0;
}

int border_frame(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int32_t t = static_cast<int32_t>(
        std::clamp(Param(p, n, 0, 8.f), 0.f, 64.f));
    if (t <= 0) return 0;
    FillRect(f, 0, 0, f->width, t, 0, 1.f);
    FillRect(f, 0, f->height - t, f->width, t, 0, 1.f);
    FillRect(f, 0, 0, t, f->height, 0, 1.f);
    FillRect(f, f->width - t, 0, t, f->height, 0, 1.f);
    return 0;
}

int corner_marks(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int32_t len = static_cast<int32_t>(
        std::clamp(Param(p, n, 0, 24.f), 4.f, 64.f));
    constexpr int32_t kT = 2;
    const int32_t w = f->width, h = f->height;
    for (int32_t sy = 0; sy < 2; ++sy) {
        for (int32_t sx = 0; sx < 2; ++sx) {
            FillRect(f, sx ? w - len : 0, sy ? h - kT : 0, len, kT, 255,
                     1.f);
            FillRect(f, sx ? w - kT : 0, sy ? h - len : 0, kT, len, 255,
                     1.f);
        }
    }
    return 0;
}

// ── style ───────────────────────────────────────────────────────────
// p0 threshold 0..255 → AdjustCurves with the solarize LUT (exact).
int solarize(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int thr = static_cast<int>(
        std::clamp(Param(p, n, 0, 128.f), 0.f, 255.f));
    std::array<uint8_t, 256> lut{};
    for (int i = 0; i < 256; ++i) {
        lut[static_cast<size_t>(i)] =
            static_cast<uint8_t>(i > thr ? 255 - i : i);
    }
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustCurves(px, w, h, lut.data(), lut.data(),
                                  lut.data(), nullptr);
    });
}

// p0 level 0..255 → AdjustThreshold (host luma threshold).
int threshold_bw(XER_VideoFrame* f, const float* p, int32_t n) {
    const int lvl = static_cast<int>(
        std::clamp(Param(p, n, 0, 128.f), 0.f, 255.f));
    return ApplySkia(f, [&](std::vector<uint32_t>* px, int w, int h) {
        return Skia::AdjustThreshold(px, w, h, lvl);
    });
}

// p0 spacing px 2..16, p1 strength 0..1 — ported.
int scanlines(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int32_t spacing = static_cast<int32_t>(
        std::clamp(Param(p, n, 0, 4.f), 2.f, 16.f));
    const float k = 1.f - std::clamp(Param(p, n, 1, 0.5f), 0.f, 1.f);
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

void HueToBgr(float deg, uint8_t* out) {
    float h = std::fmod(deg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    const float x = h / 60.0f;
    const int i = static_cast<int>(x);
    const float fr = x - static_cast<float>(i);
    float r, g, b;
    switch (i) {
        case 0:  r = 1.0f;     g = fr;       b = 0.0f;     break;
        case 1:  r = 1.0f - fr; g = 1.0f;    b = 0.0f;     break;
        case 2:  r = 0.0f;     g = 1.0f;     b = fr;       break;
        case 3:  r = 0.0f;     g = 1.0f - fr; b = 1.0f;    break;
        case 4:  r = fr;       g = 0.0f;     b = 1.0f;     break;
        default: r = 1.0f;     g = 0.0f;     b = 1.0f - fr; break;
    }
    out[0] = static_cast<uint8_t>(b * 255.0f + 0.5f);
    out[1] = static_cast<uint8_t>(g * 255.0f + 0.5f);
    out[2] = static_cast<uint8_t>(r * 255.0f + 0.5f);
}

float Luma(const uint8_t* px) {  // Rec.601 on B,G,R
    return 0.114f * px[0] + 0.587f * px[1] + 0.299f * px[2];
}

// p0 threshold 0..255, p1 hue deg 0..360 — ported Sobel edges tinted.
int neon_edges(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float thr = std::clamp(Param(p, n, 0, 40.f), 0.f, 255.f);
    uint8_t hue[3];
    HueToBgr(std::clamp(Param(p, n, 1, 300.f), 0.f, 360.f), hue);
    const int32_t w = f->width, h = f->height;
    std::vector<float> lum(static_cast<size_t>(w) * h);
    for (int32_t y = 0; y < h; ++y) {
        const uint8_t* row =
            f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x) {
            lum[static_cast<size_t>(y) * w + x] =
                Luma(row + static_cast<size_t>(x) * 4);
        }
    }
    for (int32_t y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x, row += 4) {
            float mag = 0.f;
            if (x > 0 && x + 1 < w && y > 0 && y + 1 < h) {
                const float* c =
                    lum.data() + static_cast<size_t>(y) * w + x;
                const float gx = -c[-w - 1] + c[-w + 1] - 2.f * c[-1] +
                                 2.f * c[1] - c[w - 1] + c[w + 1];
                const float gy = c[w - 1] + 2.f * c[w] + c[w + 1] -
                                 c[-w - 1] - 2.f * c[-w] - c[-w + 1];
                mag = std::sqrt(gx * gx + gy * gy);
            }
            const float t =
                mag > thr ? std::min(1.f, mag / 255.f) : 0.f;
            row[0] = static_cast<uint8_t>(hue[0] * t);
            row[1] = static_cast<uint8_t>(hue[1] * t);
            row[2] = static_cast<uint8_t>(hue[2] * t);
        }
    }
    return 0;
}

// p0 cell px 4..32 — ported halftone dots.
int halftone(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int cs = std::clamp(
        n > 0 ? static_cast<int>(p[0] + 0.5f) : 8, 4, 32);
    const int w = f->width, h = f->height, st = f->stride;
    const float half = cs * 0.5f;
    for (int cy = 0; cy < h; cy += cs) {
        const int y1 = std::min(cy + cs, h);
        for (int cx = 0; cx < w; cx += cs) {
            const int x1 = std::min(cx + cs, w);
            double sum = 0.0;
            for (int y = cy; y < y1; ++y) {
                const uint8_t* row = f->data + y * st;
                for (int x = cx; x < x1; ++x) {
                    sum += Luma(row + x * 4);
                }
            }
            const float avg = static_cast<float>(
                sum / ((y1 - cy) * (x1 - cx)));
            const float r = half * (1.f - avg / 255.f);
            const float cxc = cx + (x1 - cx) * 0.5f;
            const float cyc = cy + (y1 - cy) * 0.5f;
            for (int y = cy; y < y1; ++y) {
                uint8_t* row = f->data + y * st;
                for (int x = cx; x < x1; ++x) {
                    const float dx = x + 0.5f - cxc,
                                dy = y + 0.5f - cyc;
                    const uint8_t v =
                        dx * dx + dy * dy <= r * r ? 0 : 255;
                    uint8_t* px = row + x * 4;
                    px[0] = px[1] = px[2] = v;
                }
            }
        }
    }
    return 0;
}

// p0 angle deg, p1 strength 0..2 — ported (per-channel colour emboss;
// the bridge FilterEmboss outputs grayscale, semantics differ).
int emboss_color(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float rad = Param(p, n, 0, 135.f) * (kPi / 180.f);
    const float s = Param(p, n, 1, 1.f);
    const int dx = static_cast<int>(std::lround(std::cos(rad)));
    const int dy = static_cast<int>(std::lround(std::sin(rad)));
    const int w = f->width, h = f->height, st = f->stride;
    const std::vector<uint8_t> src(f->data,
                                   f->data + static_cast<size_t>(st) * h);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + y * st;
        const int ny = std::clamp(y + dy, 0, h - 1);
        const uint8_t* srow = src.data() + ny * st;
        const uint8_t* crow = src.data() + y * st;
        for (int x = 0; x < w; ++x) {
            const int nx = std::clamp(x + dx, 0, w - 1);
            const uint8_t* nb = srow + nx * 4;
            const uint8_t* cur = crow + x * 4;
            uint8_t* px = row + x * 4;
            for (int c = 0; c < 3; ++c) {
                px[c] = Clamp8f(128.f + s *
                                    (static_cast<float>(cur[c]) -
                                     nb[c]) +
                                0.5f);
            }
        }
    }
    return 0;
}

// p0 threshold 0..255, p1 intensity 0..4 — ported Sobel-magnitude add.
int edge_glow(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float th = Param(p, n, 0, 40.f);
    const float k = Param(p, n, 1, 1.5f);
    const int w = f->width, h = f->height, st = f->stride;
    std::vector<float> gray(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int x = 0; x < w; ++x) {
            gray[static_cast<size_t>(y) * w + x] =
                Luma(row + static_cast<size_t>(x) * 4);
        }
    }
    const auto g = [&](int x, int y) {
        return gray[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w +
                    std::clamp(x, 0, w - 1)];
    };
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int x = 0; x < w; ++x) {
            const float gx = g(x + 1, y - 1) + 2 * g(x + 1, y) +
                             g(x + 1, y + 1) - g(x - 1, y - 1) -
                             2 * g(x - 1, y) - g(x - 1, y + 1);
            const float gy = g(x - 1, y + 1) + 2 * g(x, y + 1) +
                             g(x + 1, y + 1) - g(x - 1, y - 1) -
                             2 * g(x, y - 1) - g(x + 1, y - 1);
            const float mag = std::sqrt(gx * gx + gy * gy) * 0.25f;
            if (mag > th) {
                const float add = mag * k;
                uint8_t* px = row + static_cast<size_t>(x) * 4;
                px[0] = Clamp8f(px[0] + add + 0.5f);
                px[1] = Clamp8f(px[1] + add + 0.5f);
                px[2] = Clamp8f(px[2] + add + 0.5f);
            }
        }
    }
    return 0;
}

void HueRgb(float deg, float* r, float* g, float* b) {
    float h = std::fmod(deg, 360.0f);
    if (h < 0.0f) h += 360.0f;
    h /= 60.0f;
    const float x = 1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f);
    float rr = 0.f, gg = 0.f, bb = 0.f;
    switch (static_cast<int>(h)) {
        case 0:  rr = 1.f; gg = x;   break;
        case 1:  rr = x;   gg = 1.f; break;
        case 2:  gg = 1.f; bb = x;   break;
        case 3:  gg = x;   bb = 1.f; break;
        case 4:  rr = x;   bb = 1.f; break;
        default: rr = 1.f; bb = x;   break;
    }
    *r = rr * 255.f;
    *g = gg * 255.f;
    *b = bb * 255.f;
}

// p0 shadow hue, p1 highlight hue — ported split-tone.
int tone_split(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    float sr, sg, sb, hr, hg, hb;
    HueRgb(Param(p, n, 0, 220.f), &sr, &sg, &sb);
    HueRgb(Param(p, n, 1, 40.f), &hr, &hg, &hb);
    const int w = f->width, h = f->height, st = f->stride;
    for (int y = 0; y < h; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int x = 0; x < w; ++x) {
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            const float l = Luma(px);
            const float k = 0.3f * std::fabs(l - 128.f) / 128.f;
            const float tb = l < 128.f ? sb : hb;
            const float tg = l < 128.f ? sg : hg;
            const float tr = l < 128.f ? sr : hr;
            px[0] = Clamp8f(px[0] + (tb - px[0]) * k + 0.5f);
            px[1] = Clamp8f(px[1] + (tg - px[1]) * k + 0.5f);
            px[2] = Clamp8f(px[2] + (tr - px[2]) * k + 0.5f);
        }
    }
    return 0;
}

// ── audio ───────────────────────────────────────────────────────────
// p0 delay ms 1..2000, p1 feedback 0..0.95, p2 mix 0..1 — ported
// (CircularDelayLine's read-then-write API can't express the feedback
// term: the write value needs the just-read output).
int echo(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b)) return 0;
    const float delay_ms =
        std::clamp(Param(p, n, 0, 350.f), 1.f, 2000.f);
    const float feedback =
        std::clamp(Param(p, n, 1, 0.35f), 0.f, 0.95f);
    const float mix = std::clamp(Param(p, n, 2, 0.4f), 0.f, 1.f);
    const int32_t ch = b->channels;
    const int32_t delay = std::max<int32_t>(
        1, static_cast<int32_t>(delay_ms * 0.001f * b->sample_rate +
                                0.5f));
    std::vector<float> line(static_cast<size_t>(delay) * ch, 0.f);
    const float dry = 1.f - mix;
    int32_t pos = 0;
    for (int32_t fr = 0; fr < b->frames; ++fr) {
        float* frame = b->samples + static_cast<size_t>(fr) * ch;
        float* tap = line.data() + static_cast<size_t>(pos) * ch;
        for (int32_t c = 0; c < ch; ++c) {
            const float delayed = tap[c];
            const float in = frame[c];
            frame[c] = in * dry + delayed * mix;
            tap[c] = in + delayed * feedback;
        }
        if (++pos >= delay) pos = 0;
    }
    return 0;
}

// p0 rate Hz 0.1..20, p1 depth 0..1 — ported (effects_rack ModTremolo
// uses a different gain curve/phase).
int tremolo(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b)) return 0;
    const float rate = std::clamp(Param(p, n, 0, 5.f), 0.1f, 20.f);
    const float depth = std::clamp(Param(p, n, 1, 0.6f), 0.f, 1.f);
    const double inc = 2.0 * kPi * rate / b->sample_rate;
    double phase = 0.0;
    const int32_t ch = b->channels;
    for (int32_t fr = 0; fr < b->frames; ++fr) {
        const float gain = 1.f - depth * 0.5f *
                                      static_cast<float>(
                                          1.0 + std::sin(phase));
        float* frame = b->samples + static_cast<size_t>(fr) * ch;
        for (int32_t c = 0; c < ch; ++c) frame[c] *= gain;
        phase += inc;
        if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
    return 0;
}

// p0 rate Hz 0.1..20, p1 depth 0..1 — ported (no equivalent).
int autopan(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b) || b->channels < 2) return 0;
    const float rate = std::clamp(Param(p, n, 0, 1.f), 0.1f, 20.f);
    const float depth = std::clamp(Param(p, n, 1, 0.8f), 0.f, 1.f);
    const double inc = 2.0 * kPi * rate / b->sample_rate;
    double phase = 0.0;
    const int32_t ch = b->channels;
    for (int32_t fr = 0; fr < b->frames; ++fr) {
        const float pan =
            0.5f + 0.5f * depth * static_cast<float>(std::sin(phase));
        const float angle = pan * (kPi * 0.5f);
        const float gl = std::cos(angle);
        const float gr = std::sin(angle);
        float* frame = b->samples + static_cast<size_t>(fr) * ch;
        frame[0] *= gl;
        frame[1] *= gr;
        phase += inc;
        if (phase >= 2.0 * kPi) phase -= 2.0 * kPi;
    }
    return 0;
}

// p0 bits 1..16, p1 downsample 1..64 — ported (effects_rack
// DistortionBitcrush uses a different (floor/normalized) quantizer
// and is mono-per-instance; per-channel holds are required here).
int bitcrusher(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b)) return 0;
    const int bits =
        std::clamp(static_cast<int>(Param(p, n, 0, 8.f)), 1, 16);
    const int factor =
        std::clamp(static_cast<int>(Param(p, n, 1, 4.f)), 1, 64);
    const float scale = std::ldexp(1.f, bits - 1);
    std::vector<float> hold(static_cast<size_t>(b->channels), 0.f);
    for (int32_t i = 0; i < b->frames; ++i) {
        float* s = b->samples + static_cast<size_t>(i) * b->channels;
        if (i % factor == 0) {
            for (int32_t c = 0; c < b->channels; ++c) {
                hold[c] = std::round(s[c] * scale) / scale;
            }
        }
        for (int32_t c = 0; c < b->channels; ++c) s[c] = hold[c];
    }
    return 0;
}

// p0 cutoff Hz 100..18000 — ported one-pole lowpass (no onepole
// equivalent in effects_rack; per-channel state required).
int onepole_lp(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b)) return 0;
    const float cutoff =
        std::clamp(Param(p, n, 0, 4000.f), 100.f, 18000.f);
    const float a = std::min(
        1.f - std::exp(-2.f * kPi * cutoff / b->sample_rate), 1.f);
    std::vector<float> y(static_cast<size_t>(b->channels), 0.f);
    for (int32_t i = 0; i < b->frames; ++i) {
        float* s = b->samples + static_cast<size_t>(i) * b->channels;
        for (int32_t c = 0; c < b->channels; ++c) {
            y[c] += a * (s[c] - y[c]);
            s[c] = y[c];
        }
    }
    return 0;
}

// p0 drive 1..20 → XER::Audio::FX::DistortionTube — tanh(x*drive)*g
// with output_gain = 1/tanh(drive) reproduces the DLL formula
// tanh(x*drive)/tanh(drive) exactly.
int soft_clip(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b)) return 0;
    const float drive = std::clamp(Param(p, n, 0, 4.f), 1.f, 20.f);
    const float inv_norm = 1.f / std::tanh(drive);
    XER::Audio::FX::DistortionTube tube;
    tube.SetDrive(drive);
    tube.SetOutputGain(inv_norm);
    const size_t total =
        static_cast<size_t>(b->frames) * b->channels;
    return tube.ProcessBlock(b->samples, total, b->samples) ? 0 : 1;
}

// p0 amount 0..1 (wet blend, default 1) — AI voice isolation / noise
// reduction on the staged GTCRN ONNX model (voice_isolate.cc —
// 16 kHz streaming enhancement per channel, resampled in/out,
// blended with the dry signal by `amount`). Unlike the DSP kernels
// above this one requires a staged model file and a loadable ONNX
// backend: when either is missing it fails with a reason instead of
// silently passing audio through — the bounce workflow surfaces that
// failure to the user.
int voice_isolate(XER_AudioBuffer* b, const float* p, int32_t n) {
    if (!AudioValid(b)) return 0;
    const float amount = std::clamp(Param(p, n, 0, 1.f), 0.f, 1.f);
    std::string error;
    if (!VoiceIsolate::ProcessBuffer(b->samples, b->frames,
                                     b->channels, b->sample_rate,
                                     amount, &error)) {
        return 1;
    }
    return 0;
}

// Smart Reframe is a geometric, track-driven effect — the compositor
// applies it at the warp stage. This identity keeps the registered ABI
// entry inert for any generic pixel-fx path that still invokes it.
int smart_reframe_noop(XER_VideoFrame* /*frame*/, const float* /*p*/,
                       int32_t /*n*/) {
    return 0;
}

PluginDesc Desc(const char* id, const char* name, XER_PluginKind kind,
                XER_ApplyVideoFn video, XER_ApplyAudioFn audio) {
    PluginDesc d;
    d.id = id;
    d.name = name;
    d.vendor = "XER";
    d.file = "builtin";
    d.kind = kind;
    d.video_fn = video;
    d.audio_fn = audio;
    d.builtin = true;
    return d;
}

}  // namespace

const std::vector<PluginDesc>& Effects() {
    static const std::vector<PluginDesc> kEffects = {
        // ── video (former xer_fx_adjust) ──
        Desc("xer.fx.exposure", "Exposure", XER_PLUGIN_VIDEO_FX,
             &exposure, nullptr),
        Desc("xer.fx.gamma", "Gamma", XER_PLUGIN_VIDEO_FX, &gamma_corr,
             nullptr),
        Desc("xer.fx.levels", "Levels", XER_PLUGIN_VIDEO_FX, &levels,
             nullptr),
        Desc("xer.fx.auto_contrast", "Auto Contrast",
             XER_PLUGIN_VIDEO_FX, &auto_contrast, nullptr),
        Desc("xer.fx.temperature", "Temperature", XER_PLUGIN_VIDEO_FX,
             &temperature, nullptr),
        Desc("xer.fx.tint", "Tint", XER_PLUGIN_VIDEO_FX, &tint,
             nullptr),
        Desc("xer.fx.vibrance", "Vibrance", XER_PLUGIN_VIDEO_FX,
             &vibrance, nullptr),
        Desc("xer.fx.lift_gamma_gain", "Lift/Gamma/Gain",
             XER_PLUGIN_VIDEO_FX, &lift_gamma_gain, nullptr),
        // ── video (former xer_fx_blur) ──
        Desc("xer.fx.directional_blur", "Directional Blur",
             XER_PLUGIN_VIDEO_FX, &directional_blur, nullptr),
        Desc("xer.fx.radial_blur", "Radial Blur", XER_PLUGIN_VIDEO_FX,
             &radial_blur, nullptr),
        Desc("xer.fx.zoom_blur", "Zoom Blur", XER_PLUGIN_VIDEO_FX,
             &zoom_blur, nullptr),
        Desc("xer.fx.fast_gaussian", "Fast Gaussian",
             XER_PLUGIN_VIDEO_FX, &fast_gaussian, nullptr),
        Desc("xer.fx.unsharp_mask", "Unsharp Mask",
             XER_PLUGIN_VIDEO_FX, &unsharp_mask, nullptr),
        Desc("xer.fx.median3", "Median 3x3", XER_PLUGIN_VIDEO_FX,
             &median3, nullptr),
        // ── video (former xer_fx_color) ──
        Desc("xer.fx.hue_rotate", "Hue Rotate", XER_PLUGIN_VIDEO_FX,
             &hue_rotate, nullptr),
        Desc("xer.fx.sepia", "Sepia", XER_PLUGIN_VIDEO_FX, &sepia_tone,
             nullptr),
        Desc("xer.fx.duotone", "Duotone", XER_PLUGIN_VIDEO_FX,
             &duotone, nullptr),
        Desc("xer.fx.channel_mixer", "Channel Mixer",
             XER_PLUGIN_VIDEO_FX, &channel_mixer, nullptr),
        Desc("xer.fx.invert_lum", "Invert Luma", XER_PLUGIN_VIDEO_FX,
             &invert_lum, nullptr),
        Desc("xer.fx.color_isolate", "Color Isolate",
             XER_PLUGIN_VIDEO_FX, &color_isolate, nullptr),
        Desc("xer.fx.rgb_swap", "RGB Swap", XER_PLUGIN_VIDEO_FX,
             &rgb_swap, nullptr),
        Desc("xer.fx.saturation_boost", "Saturation Boost",
             XER_PLUGIN_VIDEO_FX, &saturation_boost, nullptr),
        // ── video (former xer_fx_distort) ──
        Desc("xer.fx.wave_h", "Wave Horizontal", XER_PLUGIN_VIDEO_FX,
             &wave_h, nullptr),
        Desc("xer.fx.wave_v", "Wave Vertical", XER_PLUGIN_VIDEO_FX,
             &wave_v, nullptr),
        Desc("xer.fx.ripple", "Ripple", XER_PLUGIN_VIDEO_FX, &ripple,
             nullptr),
        Desc("xer.fx.mirror", "Mirror", XER_PLUGIN_VIDEO_FX,
             &mirror_quad, nullptr),
        Desc("xer.fx.tile", "Tile", XER_PLUGIN_VIDEO_FX, &tile,
             nullptr),
        Desc("xer.fx.twirl", "Twirl", XER_PLUGIN_VIDEO_FX, &twirl,
             nullptr),
        // ── video (former xer_fx_overlay) ──
        Desc("xer.fx.film_grain", "Film Grain", XER_PLUGIN_VIDEO_FX,
             &film_grain, nullptr),
        Desc("xer.fx.letterbox", "Letterbox", XER_PLUGIN_VIDEO_FX,
             &letterbox, nullptr),
        Desc("xer.fx.vignette_soft", "Vignette Soft",
             XER_PLUGIN_VIDEO_FX, &vignette_soft, nullptr),
        Desc("xer.fx.grid_lines", "Grid Lines", XER_PLUGIN_VIDEO_FX,
             &grid_lines, nullptr),
        Desc("xer.fx.border_frame", "Border Frame",
             XER_PLUGIN_VIDEO_FX, &border_frame, nullptr),
        Desc("xer.fx.corner_marks", "Corner Marks",
             XER_PLUGIN_VIDEO_FX, &corner_marks, nullptr),
        // ── video (former xer_fx_style) ──
        Desc("xer.fx.solarize", "Solarize", XER_PLUGIN_VIDEO_FX,
             &solarize, nullptr),
        Desc("xer.fx.threshold_bw", "Threshold B/W",
             XER_PLUGIN_VIDEO_FX, &threshold_bw, nullptr),
        Desc("xer.fx.scanlines", "Scanlines", XER_PLUGIN_VIDEO_FX,
             &scanlines, nullptr),
        Desc("xer.fx.neon_edges", "Neon Edges", XER_PLUGIN_VIDEO_FX,
             &neon_edges, nullptr),
        Desc("xer.fx.halftone", "Halftone", XER_PLUGIN_VIDEO_FX,
             &halftone, nullptr),
        Desc("xer.fx.emboss_color", "Emboss Color",
             XER_PLUGIN_VIDEO_FX, &emboss_color, nullptr),
        Desc("xer.fx.edge_glow", "Edge Glow", XER_PLUGIN_VIDEO_FX,
             &edge_glow, nullptr),
        Desc("xer.fx.tone_split", "Tone Split", XER_PLUGIN_VIDEO_FX,
             &tone_split, nullptr),
        // ── video (Smart Reframe — geometric, compositor-driven) ──
        // The descriptor registers the canonical id, params and picker
        // entry; actual cropping is NOT a pixel op — it needs the
        // clip's analyzed subject track + current source time, so
        // compositor.cc applies it at the warp stage and skips this
        // type in ApplyClipEffects. The identity fn keeps the ABI
        // entry harmless when a generic video-fx path invokes it.
        [] {
            PluginDesc d =
                Desc("xer.fx.smart_reframe", "Smart Reframe",
                     XER_PLUGIN_VIDEO_FX, &smart_reframe_noop, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"target_ratio", 0.1f, 4.0f, 0.5625f},  // w/h (9:16)
                {"smoothing", 0.0f, 1.0f, 0.5f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        // ── audio (former xer_audio_fx) ──
        Desc("xer.af.echo", "Echo", XER_PLUGIN_AUDIO_FX, nullptr,
             &echo),
        Desc("xer.af.tremolo", "Tremolo", XER_PLUGIN_AUDIO_FX, nullptr,
             &tremolo),
        Desc("xer.af.autopan", "Auto Pan", XER_PLUGIN_AUDIO_FX,
             nullptr, &autopan),
        Desc("xer.af.bitcrusher", "Bitcrusher", XER_PLUGIN_AUDIO_FX,
             nullptr, &bitcrusher),
        Desc("xer.af.onepole_lp", "Lowpass", XER_PLUGIN_AUDIO_FX,
             nullptr, &onepole_lp),
        Desc("xer.af.soft_clip", "Soft Clip", XER_PLUGIN_AUDIO_FX,
             nullptr, &soft_clip),
        // ── audio (AI denoise — GTCRN ONNX, voice_isolate.cc) ──
        [] {
            PluginDesc d =
                Desc("xer.af.voice_isolate", "Voice Isolate (AI)",
                     XER_PLUGIN_AUDIO_FX, nullptr, &voice_isolate);
            static const XER_ParamDesc kParams[] = {
                {"amount", 0.f, 1.f, 1.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
    };
    return kEffects;
}

std::vector<std::string> Ids() {
    std::vector<std::string> out;
    out.reserve(Effects().size());
    for (const PluginDesc& d : Effects()) {
        out.push_back(d.id);
    }
    return out;
}

}  // namespace XER::VideoEditor::BuiltinFx
