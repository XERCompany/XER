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
#include <string>
#include <vector>

#include "audio/effects_rack/fx_distortion_tube.h"
#include "audio/effects_rack/fx_dynamics_compressor.h"
#include "audio/effects_rack/fx_dynamics_expander.h"
#include "audio/effects_rack/fx_dynamics_gate.h"
#include "audio/effects_rack/fx_dynamics_limiter.h"
#include "audio/effects_rack/fx_eq_graphic.h"
#include "audio/effects_rack/fx_eq_parametric.h"
#include "audio/effects_rack/fx_mod_chorus.h"
#include "audio/effects_rack/fx_mod_extra.h"
#include "audio/effects_rack/fx_mod_flanger.h"
#include "audio/effects_rack/fx_mod_phaser.h"
#include "audio/effects_rack/fx_pitch_auto_tune_correct.h"
#include "audio/effects_rack/fx_reverb_algorithmic.h"
#include "audio/effects_rack/fx_reverb_convolution.h"
#include "parallel_rows.h"
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

// ── film look — forward decls ─────────────────────────────────────
// Time-aware kernels defined in the "film look" section below; the
// ABI fns wrap them at t=0/seed=0 while the compositor calls them
// through ApplyFilmEffect with the clip's media time + id hash.
int film_grain_kernel(XER_VideoFrame* f, const float* p, int32_t n,
                      double media_time_s, std::uint64_t clip_seed);

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
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
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
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
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
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
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
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
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
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
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
    // Column-major kernel — columns are disjoint output units.
    VeParallelFor(w, [&](int32_t xa, int32_t xb) {
    for (int32_t x = xa; x < xb; ++x) {
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
    });
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
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
    return 0;
}

int mirror_quad(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int w = f->width, h = f->height;
    const int mode =
        std::clamp(static_cast<int>(Param(p, n, 0, 0.f)), 0, 3);
    const int hw = w / 2, hh = h / 2;
    // Writes only ever target the "mirror" half of a row (or never
    // write for a pass-through pixel) while reads land in the fixed
    // half — row ranges stay disjoint across every mode.
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
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
    });
    return 0;
}

int tile(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int w = f->width, h = f->height;
    const int count =
        std::clamp(static_cast<int>(Param(p, n, 0, 3.f)), 2, 8);
    const std::vector<uint8_t> src = Snapshot(f);
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        const uint8_t* srow =
            src.data() +
            static_cast<size_t>((y * count) % h) * f->stride;
        for (int x = 0; x < w; ++x) {
            std::copy_n(srow + static_cast<size_t>((x * count) % w) * 4,
                        4, row + static_cast<size_t>(x) * 4);
        }
    }
    });
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
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
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
    });
    return 0;
}

// ── overlay ─────────────────────────────────────────────────────────
// p0 amount 0..100, p1 monochrome 0/1, p2 block px 1..8, p3 seed —
// v2 kernel (luma-weighted, block-sized, deterministic per
// (clip,time)). The ABI form renders the static t=0/seed=0 variant
// for generic apply paths; the compositor dispatches the animated
// one via ApplyFilmEffect (see the film-look section).
int film_grain(XER_VideoFrame* f, const float* p, int32_t n) {
    return film_grain_kernel(f, p, n, 0.0, 0);
}

// p0 bar fraction 0..0.45 — ported (draw op).
int letterbox(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float frac =
        std::clamp(Param(p, n, 0, 0.12f), 0.f, 0.45f);
    const int32_t bar =
        static_cast<int32_t>(std::lround(frac * f->height));
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        if (y >= bar && y < f->height - bar) continue;
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            px[0] = px[1] = px[2] = 0;
            px[3] = 255;
        }
    }
    });
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
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
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
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        if (y % spacing != spacing - 1) continue;
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, row += 4) {
            row[0] = static_cast<uint8_t>(row[0] * k);
            row[1] = static_cast<uint8_t>(row[1] * k);
            row[2] = static_cast<uint8_t>(row[2] * k);
        }
    }
    });
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
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        const uint8_t* row =
            f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < w; ++x) {
            lum[static_cast<size_t>(y) * w + x] =
                Luma(row + static_cast<size_t>(x) * 4);
        }
    }
    });
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
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
    });
    return 0;
}

// p0 cell px 4..32 — ported halftone dots.
int halftone(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const int cs = std::clamp(
        n > 0 ? static_cast<int>(p[0] + 0.5f) : 8, 4, 32);
    const int w = f->width, h = f->height, st = f->stride;
    const float half = cs * 0.5f;
    // Cell-grid kernel — cell rows are disjoint output units.
    const int cell_rows = (h + cs - 1) / cs;
    VeParallelFor(cell_rows, [&](int ra, int rb) {
    for (int ri = ra; ri < rb; ++ri) {
        const int cy = ri * cs;
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
    });
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
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
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
    });
    return 0;
}

// p0 threshold 0..255, p1 intensity 0..4 — ported Sobel-magnitude add.
int edge_glow(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float th = Param(p, n, 0, 40.f);
    const float k = Param(p, n, 1, 1.5f);
    const int w = f->width, h = f->height, st = f->stride;
    std::vector<float> gray(static_cast<size_t>(w) * h);
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
        const uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int x = 0; x < w; ++x) {
            gray[static_cast<size_t>(y) * w + x] =
                Luma(row + static_cast<size_t>(x) * 4);
        }
    }
    });
    const auto g = [&](int x, int y) {
        return gray[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w +
                    std::clamp(x, 0, w - 1)];
    };
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
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
    });
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
    VeParallelRows(h, [&](int ya, int yb) {
    for (int y = ya; y < yb; ++y) {
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
    });
    return 0;
}

// ── film look ─────────────────────────────────────────────────────
// Signature-analog kit (docs/wiki/research/2026-11-24 §1.1): luma-
// weighted grain, halation, gate weave, light leak, exposure
// flicker, dust/scratch — plus an upgraded vignette. The animated
// members are deterministic functions of (clip_seed, media_time,
// params): the compositor calls them via BuiltinFx::ApplyFilmEffect
// with the resolved media time, so each frame gets a fresh field
// WITHOUT strobing and the result stays a pure input to the
// effect-frame cache (media time is already the cache key's time
// component). The registered ABI fns run the same kernels at
// t=0/seed=0 — a static variant for generic PluginHost::ApplyVideo
// paths. No scratch allocations in the hot loops: noise/motion
// fields are procedural (per-block hash), and the one resample
// kernel reuses the shared Snapshot() idiom.

// splitmix64 finalizer → decorrelated uint32/uint64 streams. All
// film-kit randomness flows through this so results are stable
// across runs, platforms and compositor threads.
std::uint64_t Sm64Next(std::uint64_t* s) {
    *s += 0x9E3779B97F4A7C15ull;
    std::uint64_t z = *s;
    z ^= z >> 30;
    z *= 0xBF58476D1CE4E5B9ull;
    z ^= z >> 27;
    z *= 0x94D049BB133111EBull;
    z ^= z >> 31;
    return z;
}

std::uint32_t HashBits(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdull;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ull;
    x ^= x >> 33;
    return static_cast<std::uint32_t>(x);
}

float HashToUnit(std::uint32_t h) {  // (h>>8) in [0,2^24) → [-1,1)
    return static_cast<float>(h >> 8) * (1.f / 8388608.f) - 1.f;
}

// Per-(clip, quantized time, user seed) noise seed. Time quantizes
// at 1/240 s — finer than any delivery rate, so every displayed
// frame gets its own deterministic field and re-rendering the same
// instant bit-matches (the strobing-free contract).
std::uint32_t FilmSeed(double media_time_s, std::uint64_t clip_seed,
                       float user_seed) {
    const std::int64_t tq = static_cast<std::int64_t>(
        std::llround(media_time_s * 240.0));
    return HashBits(
        clip_seed ^ static_cast<std::uint64_t>(tq) *
                        0x9E3779B97F4A7C15ull ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                                       std::lround(user_seed))) *
            0xD1B54A32D192ED03ull);
}

// Triangular (grain-ish) block noise in [-1,1) — two hashed
// uniforms summed. `block` is the linear block index, `channel`
// decorrelates the B/G/R planes in color mode.
float GrainNoise(std::uint32_t seed, std::uint32_t block,
                 std::uint32_t channel) {
    const std::uint64_t x =
        (static_cast<std::uint64_t>(seed) << 32) ^ block ^
        static_cast<std::uint64_t>(channel) * 0x9E3779B1ull;
    return (HashToUnit(HashBits(x)) +
            HashToUnit(HashBits(x ^ 0xA511E9B3u))) *
           0.5f;
}

// Proper film grain v2 — p0 amount 0..100, p1 mono 0/1, p2 block px
// 1..8 (nearest-upsampled noise cells), p3 seed. Per-frame animated:
// the field is a pure function of (clip, media time) so it
// regenerates deterministically — no boiling sync between adjacent
// frames, identical output on re-render of the same instant.
// Amplitude is shadow-weighted (sqrt(1-luma)): strong in shadows and
// mids, fading to none at white, like real stock.
int film_grain_kernel(XER_VideoFrame* f, const float* p, int32_t n,
                      double media_time_s, std::uint64_t clip_seed) {
    if (!VideoValid(f)) return 1;
    const float amount = std::clamp(Param(p, n, 0, 20.f), 0.f, 100.f);
    const bool mono = Param(p, n, 1, 1.f) >= 0.5f;
    const int32_t size = std::clamp(
        static_cast<int32_t>(std::lround(Param(p, n, 2, 1.f))), 1, 8);
    if (amount <= 0.f) return 0;
    const int32_t w = f->width, h = f->height, st = f->stride;
    const int32_t fw = (w + size - 1) / size;
    const std::uint32_t seed =
        FilmSeed(media_time_s, clip_seed, Param(p, n, 3, 0.f));
    const float scale = amount * 0.5f;
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        const int32_t by = y / size;
        for (int32_t bx = 0; bx < fw; ++bx) {
            const std::uint32_t block =
                static_cast<std::uint32_t>(by) *
                    static_cast<std::uint32_t>(fw) +
                static_cast<std::uint32_t>(bx);
            const float n0 = GrainNoise(seed, block, 0);
            const float n1 = mono ? 0.f : GrainNoise(seed, block, 1);
            const float n2 = mono ? 0.f : GrainNoise(seed, block, 2);
            const int32_t x1 = std::min(w, (bx + 1) * size);
            for (int32_t x = bx * size; x < x1; ++x) {
                uint8_t* px = row + static_cast<size_t>(x) * 4;
                const float luma =
                    (0.114f * px[0] + 0.587f * px[1] + 0.299f * px[2]) *
                    (1.f / 255.f);
                const float k =
                    scale * std::sqrt(std::max(0.f, 1.f - luma));
                if (mono) {
                    const float g = n0 * k;
                    px[0] = Clamp8f(px[0] + g);
                    px[1] = Clamp8f(px[1] + g);
                    px[2] = Clamp8f(px[2] + g);
                } else {
                    px[0] = Clamp8f(px[0] + n0 * k);
                    px[1] = Clamp8f(px[1] + n1 * k);
                    px[2] = Clamp8f(px[2] + n2 * k);
                }
            }
        }
    }
    });
    return 0;
}

// Halation — p0 threshold 0..255, p1 spread px 1..64, p2 strength
// 0..1, p3 hue deg. Highlights (soft-knee luma mask × source color)
// are extracted into a half-res scratch, heavily blurred there
// (4× cheaper — halos are low-frequency anyway), multiplied toward
// the warm tint (the film red layer scatters most), then screen-
// blended back with bilinear upsample. Pure per-frame kernel.
int halation(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float th = std::clamp(Param(p, n, 0, 200.f), 0.f, 255.f);
    const float spread = std::clamp(Param(p, n, 1, 14.f), 1.f, 64.f);
    const float strength = std::clamp(Param(p, n, 2, 0.5f), 0.f, 1.f);
    if (strength <= 0.f) return 0;
    uint8_t tint[3];
    HueToBgr(std::clamp(Param(p, n, 3, 25.f), 0.f, 360.f), tint);
    const int32_t w = f->width, h = f->height, st = f->stride;
    const int32_t w2 = std::max<int32_t>(1, w / 2);
    const int32_t h2 = std::max<int32_t>(1, h / 2);
    std::vector<uint32_t> glow(static_cast<size_t>(w2) *
                                   static_cast<size_t>(h2),
                               0u);
    const float knee = std::max(1.f, 255.f - th);
    VeParallelRows(h2, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        for (int32_t x = 0; x < w2; ++x) {
            float b = 0.f, g = 0.f, r = 0.f;
            for (int32_t dy = 0; dy < 2; ++dy) {
                const int32_t sy = std::min(y * 2 + dy, h - 1);
                const uint8_t* srow =
                    f->data + static_cast<size_t>(sy) * st;
                for (int32_t dx = 0; dx < 2; ++dx) {
                    const int32_t sx = std::min(x * 2 + dx, w - 1);
                    const uint8_t* px =
                        srow + static_cast<size_t>(sx) * 4;
                    const float t = std::clamp(
                        (Luma(px) - th) / knee, 0.f, 1.f);
                    const float m = t * t * (3.f - 2.f * t);
                    b += px[0] * m;
                    g += px[1] * m;
                    r += px[2] * m;
                }
            }
            glow[static_cast<size_t>(y) * w2 + x] =
                Skia::MakeColorRGBA(static_cast<int>(r * 0.25f + 0.5f),
                                    static_cast<int>(g * 0.25f + 0.5f),
                                    static_cast<int>(b * 0.25f + 0.5f),
                                    255);
        }
    }
    });
    // Half-res gaussian: sigma halves with the image so the
    // full-res halo radius stays ≈ `spread`.
    const double sigma =
        std::max(0.5, static_cast<double>(spread) * 0.5);
    if (!Skia::FilterGaussianBlur(&glow, w2, h2, sigma, sigma)) {
        return 1;
    }
    const auto glow_at = [&](int32_t x, int32_t y) -> uint32_t {
        return glow[static_cast<size_t>(
                        std::clamp(y, 0, h2 - 1)) * w2 +
                    std::clamp(x, 0, w2 - 1)];
    };
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        const float gy = (y + 0.5f) * 0.5f - 0.5f;
        const int32_t y0 = std::clamp(static_cast<int32_t>(gy), 0,
                                      h2 - 1);
        const int32_t y1 = std::min(y0 + 1, h2 - 1);
        const float fy = gy - static_cast<float>(y0);
        for (int32_t x = 0; x < w; ++x) {
            const float gx = (x + 0.5f) * 0.5f - 0.5f;
            const int32_t x0 =
                std::clamp(static_cast<int32_t>(gx), 0, w2 - 1);
            const int32_t x1 = std::min(x0 + 1, w2 - 1);
            const float fx = gx - static_cast<float>(x0);
            const uint32_t c00 = glow_at(x0, y0);
            const uint32_t c10 = glow_at(x1, y0);
            const uint32_t c01 = glow_at(x0, y1);
            const uint32_t c11 = glow_at(x1, y1);
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            for (int32_t c = 0; c < 3; ++c) {
                // Bridge packing is R<<24|G<<16|B<<8|A; memory order
                // here is B,G,R → channel c reads shift 8*(c+1).
                const int32_t sh = 8 * (c + 1);
                const float top =
                    static_cast<float>((c00 >> sh) & 0xFF) * (1.f - fx) +
                    static_cast<float>((c10 >> sh) & 0xFF) * fx;
                const float bot =
                    static_cast<float>((c01 >> sh) & 0xFF) * (1.f - fx) +
                    static_cast<float>((c11 >> sh) & 0xFF) * fx;
                const float blurred = top + (bot - top) * fy;
                // Warm multiply keeps the halo hue; screen blend adds
                // it without crushing mids.
                const float leak =
                    blurred * (tint[c] * (1.f / 255.f)) * strength;
                px[c] = Clamp8f(
                    px[c] + (255.f - px[c]) * (leak / 255.f));
            }
        }
    }
    });
    return 0;
}

// Bilinear sample on a strided BGRA source, edge-replicated.
void BilinearBGRA(const std::vector<uint8_t>& src, int32_t w,
                  int32_t h, int32_t stride, float sx, float sy,
                  uint8_t* out) {
    sx = std::clamp(sx, 0.f, static_cast<float>(w - 1));
    sy = std::clamp(sy, 0.f, static_cast<float>(h - 1));
    const int32_t x0 = static_cast<int32_t>(sx);
    const int32_t y0 = static_cast<int32_t>(sy);
    const int32_t x1 = std::min(x0 + 1, w - 1);
    const int32_t y1 = std::min(y0 + 1, h - 1);
    const float fx = sx - static_cast<float>(x0);
    const float fy = sy - static_cast<float>(y0);
    const uint8_t* p00 =
        src.data() + static_cast<size_t>(y0) * stride +
        static_cast<size_t>(x0) * 4;
    const uint8_t* p10 =
        src.data() + static_cast<size_t>(y0) * stride +
        static_cast<size_t>(x1) * 4;
    const uint8_t* p01 =
        src.data() + static_cast<size_t>(y1) * stride +
        static_cast<size_t>(x0) * 4;
    const uint8_t* p11 =
        src.data() + static_cast<size_t>(y1) * stride +
        static_cast<size_t>(x1) * 4;
    for (int32_t c = 0; c < 4; ++c) {
        const float top = p00[c] + (p10[c] - p00[c]) * fx;
        const float bot = p01[c] + (p11[c] - p01[c]) * fx;
        out[c] = Clamp8f(top + (bot - top) * fy + 0.5f);
    }
}

// Gate weave — p0 amplitude px 0..16, p1 speed Hz 0.01..8, p2 seed.
// Projector-gate jitter: a slow dominant sine per axis plus a faster
// incommensurate rattle (phases seeded per clip) — smooth wander,
// no per-frame jumps. Whole-frame subpixel shift, edge-replicated.
int gate_weave_kernel(XER_VideoFrame* f, const float* p, int32_t n,
                      double media_time_s, std::uint64_t clip_seed) {
    if (!VideoValid(f)) return 1;
    const float amp = std::clamp(Param(p, n, 0, 1.5f), 0.f, 16.f);
    const float speed =
        std::clamp(Param(p, n, 1, 0.6f), 0.01f, 8.f);
    if (amp <= 0.f) return 0;
    std::uint64_t rs = clip_seed ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                                       std::lround(Param(p, n, 2, 0.f)))) *
            0xBF58476D1CE4E5B9ull ^ 0x67617465776176ull;
    float ph[4];
    for (float& v : ph) {
        v = static_cast<float>(Sm64Next(&rs) >> 40) *
            (kTwoPi / 16777216.f);
    }
    const float t = static_cast<float>(media_time_s);
    const float dx =
        amp * (0.62f * std::sin(kTwoPi * speed * t + ph[0]) +
               0.38f * std::sin(kTwoPi * speed * 2.71f * t + ph[1]));
    const float dy =
        amp * (0.62f * std::sin(kTwoPi * speed * 0.87f * t + ph[2]) +
               0.38f * std::sin(kTwoPi * speed * 3.13f * t + ph[3]));
    const std::vector<uint8_t> src = Snapshot(f);
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x) {
            BilinearBGRA(src, f->width, f->height, f->stride,
                         static_cast<float>(x) - dx,
                         static_cast<float>(y) - dy,
                         row + static_cast<size_t>(x) * 4);
        }
    }
    });
    return 0;
}

int gate_weave(XER_VideoFrame* f, const float* p, int32_t n) {
    return gate_weave_kernel(f, p, n, 0.0, 0);
}

// Light leak — p0 intensity 0..1, p1 hue deg, p2 seed, p3 animated
// 0/1. Three warm radial blobs pinned to seeded frame edges, centers
// sitting half off-frame so the visible part is a crescent bleeding
// inward; `animated` drifts them along the edge on slow seeded
// sines. Screen blend — leaks add light, never darken.
int light_leak_kernel(XER_VideoFrame* f, const float* p, int32_t n,
                      double media_time_s, std::uint64_t clip_seed) {
    if (!VideoValid(f)) return 1;
    const float intensity =
        std::clamp(Param(p, n, 0, 0.45f), 0.f, 1.f);
    if (intensity <= 0.f) return 0;
    uint8_t warm[3];
    HueToBgr(std::clamp(Param(p, n, 1, 30.f), 0.f, 360.f), warm);
    const bool animated = Param(p, n, 3, 1.f) >= 0.5f;
    const int32_t w = f->width, h = f->height, st = f->stride;
    const float dim = static_cast<float>(std::min(w, h));
    const float t = static_cast<float>(media_time_s);
    struct Blob {
        float cx, cy, r2, gain;
    };
    Blob blobs[3];
    std::uint64_t rs = clip_seed ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                                       std::lround(Param(p, n, 2, 0.f)))) *
            0x94D049BB133111EBull ^ 0x6C696B656C6Bull;
    for (Blob& b : blobs) {
        const int32_t edge =
            static_cast<int32_t>(Sm64Next(&rs) & 3u);
        const float u = 0.1f + 0.8f *
            (static_cast<float>(Sm64Next(&rs) >> 40) *
             (1.f / 16777216.f));
        const float out = 0.1f + 0.8f *
            (static_cast<float>(Sm64Next(&rs) >> 40) *
             (1.f / 16777216.f));
        const float r = (0.30f + 0.45f *
            (static_cast<float>(Sm64Next(&rs) >> 40) *
             (1.f / 16777216.f))) * dim;
        const float drift_hz = 0.03f + 0.09f *
            (static_cast<float>(Sm64Next(&rs) >> 40) *
             (1.f / 16777216.f));
        const float drift_ph =
            static_cast<float>(Sm64Next(&rs) >> 40) *
            (kTwoPi / 16777216.f);
        b.gain = 0.7f + 0.3f *
            (static_cast<float>(Sm64Next(&rs) >> 40) *
             (1.f / 16777216.f));
        float pos = u;
        if (animated) {
            pos += 0.18f * std::sin(kTwoPi * drift_hz * t + drift_ph);
        }
        pos = std::clamp(pos, 0.f, 1.f);
        switch (edge) {
            case 0:  b.cx = -r * out;       b.cy = pos * h; break;
            case 1:  b.cx = w + r * out;    b.cy = pos * h; break;
            case 2:  b.cx = pos * w;        b.cy = -r * out; break;
            default: b.cx = pos * w;        b.cy = h + r * out; break;
        }
        b.r2 = r * r;
    }
    VeParallelRows(h, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * st;
        for (int32_t x = 0; x < w; ++x) {
            float k = 0.f;
            for (const Blob& b : blobs) {
                const float dx = x + 0.5f - b.cx;
                const float dy = y + 0.5f - b.cy;
                const float q =
                    1.f - (dx * dx + dy * dy) / b.r2;
                if (q > 0.f) k += q * q * b.gain;
            }
            if (k <= 0.f) continue;
            const float s = std::min(k, 1.f) * intensity;
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            px[0] = Clamp8f(px[0] + (255.f - px[0]) *
                                        (warm[0] * s / 255.f));
            px[1] = Clamp8f(px[1] + (255.f - px[1]) *
                                        (warm[1] * s / 255.f));
            px[2] = Clamp8f(px[2] + (255.f - px[2]) *
                                        (warm[2] * s / 255.f));
        }
    }
    });
    return 0;
}

int light_leak(XER_VideoFrame* f, const float* p, int32_t n) {
    return light_leak_kernel(f, p, n, 0.0, 0);
}

// Film flicker — p0 amount 0..1, p1 rate Hz 0.5..24, p2 seed.
// Subtle whole-frame exposure modulation: a dominant sine at `rate`
// plus two incommensurate partials (seeded phases) ≈ the perlin-ish
// wander of a real gate/lamp. LUT-applied to RGB.
int film_flicker_kernel(XER_VideoFrame* f, const float* p, int32_t n,
                        double media_time_s, std::uint64_t clip_seed) {
    if (!VideoValid(f)) return 1;
    const float amount =
        std::clamp(Param(p, n, 0, 0.18f), 0.f, 1.f);
    if (amount <= 0.f) return 0;
    const float rate = std::clamp(Param(p, n, 1, 6.f), 0.5f, 24.f);
    std::uint64_t rs = clip_seed ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(
                                       std::lround(Param(p, n, 2, 0.f)))) *
            0xBF58476D1CE4E5B9ull ^ 0x666C69636B72ull;
    float ph[3];
    for (float& v : ph) {
        v = static_cast<float>(Sm64Next(&rs) >> 40) *
            (kTwoPi / 16777216.f);
    }
    const float t = static_cast<float>(media_time_s);
    const float gain =
        1.f + amount * 0.55f *
                  (0.5f * std::sin(kTwoPi * rate * t + ph[0]) +
                   0.3f * std::sin(kTwoPi * rate * 0.37f * t + ph[1]) +
                   0.2f * std::sin(kTwoPi * rate * 2.61f * t + ph[2]));
    std::array<uint8_t, 256> lut{};
    for (int32_t i = 0; i < 256; ++i) {
        lut[static_cast<size_t>(i)] = Clamp8f(i * gain);
    }
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        uint8_t* px = f->data + static_cast<size_t>(y) * f->stride;
        for (int32_t x = 0; x < f->width; ++x, px += 4) {
            px[0] = lut[px[0]];
            px[1] = lut[px[1]];
            px[2] = lut[px[2]];
        }
    }
    });
    return 0;
}

int film_flicker(XER_VideoFrame* f, const float* p, int32_t n) {
    return film_flicker_kernel(f, p, n, 0.0, 0);
}

// Dust & scratch — p0 density 0..1, p1 seed. Dust specks are fixed
// to the print (same every frame — static per seed, that's what
// makes them read as dust rather than noise). Vertical scratches
// live for a ~0.4 s "reel" window each — occasional, not constant —
// with a per-row ±1 px wobble.
int dust_scratch_kernel(XER_VideoFrame* f, const float* p, int32_t n,
                        double media_time_s,
                        std::uint64_t clip_seed) {
    if (!VideoValid(f)) return 1;
    const float density =
        std::clamp(Param(p, n, 0, 0.35f), 0.f, 1.f);
    if (density <= 0.f) return 0;
    const int32_t w = f->width, h = f->height;
    const std::uint64_t base =
        clip_seed ^ static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(
                            std::lround(Param(p, n, 1, 0.f)))) *
                        0x94D049BB133111EBull ^ 0x647573747263ull;
    std::uint64_t rs = base;
    const auto f01 = [&] {
        return static_cast<float>(Sm64Next(&rs) >> 40) *
               (1.f / 16777216.f);
    };
    const int32_t specks = static_cast<int32_t>(
        std::lround(density * 180.0));
    for (int32_t i = 0; i < specks; ++i) {
        const int32_t x = static_cast<int32_t>(f01() * w);
        const int32_t y = static_cast<int32_t>(f01() * h);
        const int32_t r = 1 + static_cast<int32_t>(f01() * 2.99f);
        const uint8_t v = (Sm64Next(&rs) & 1u) ? 235 : 25;
        const float a = 0.2f + 0.55f * f01();
        FillRect(f, x - r / 2, y - r / 2, r, r, v, a);
    }
    // Occasional vertical scratches — each candidate is live for one
    // ~0.36 s reel bucket, so they appear and clear instead of
    // strobing per frame.
    const int32_t candidates =
        1 + static_cast<int32_t>(density * 5.99f);
    const std::int64_t reel =
        static_cast<std::int64_t>(std::floor(media_time_s * 2.8));
    for (int32_t i = 0; i < candidates; ++i) {
        std::uint64_t sr = base ^
            static_cast<std::uint64_t>(i) * 0x2545F4914F6CDD1Dull ^
            static_cast<std::uint64_t>(reel) * 0x9E3779B97F4A7C15ull;
        if (static_cast<float>(Sm64Next(&sr) >> 40) *
                (1.f / 16777216.f) >= density * 0.55f) {
            continue;
        }
        const float x0 =
            static_cast<float>(Sm64Next(&sr) >> 40) *
            (1.f / 16777216.f) * w;
        const uint8_t v =
            (static_cast<float>(Sm64Next(&sr) >> 40) *
                 (1.f / 16777216.f) < 0.75f)
                ? 255
                : 0;
        const float a = 0.15f + 0.4f *
            (static_cast<float>(Sm64Next(&sr) >> 40) *
             (1.f / 16777216.f));
        const std::uint64_t wob = Sm64Next(&sr);
        for (int32_t y = 0; y < h; ++y) {
            const int32_t jitter = static_cast<int32_t>(
                HashBits(wob ^ static_cast<std::uint64_t>(y) *
                                   0x9E3779B1ull) & 3u) - 1;
            const int32_t x = std::clamp(
                static_cast<int32_t>(x0) + jitter, 0, w - 1);
            BlendTo(f->data + static_cast<size_t>(y) * f->stride +
                        static_cast<size_t>(x) * 4,
                    v, a);
        }
    }
    return 0;
}

int dust_scratch(XER_VideoFrame* f, const float* p, int32_t n) {
    return dust_scratch_kernel(f, p, n, 0.0, 0);
}

// Vignette 2 — p0 strength 0..1, p1 radius 0..1, p2 feather 0..1
// (falloff band width; vignette_soft's is fixed to the frame edge),
// p3 aspect 0.5..2 (>1 compresses vertically — anamorphic-style
// elliptical falloff). Pure per-frame kernel.
int vignette2(XER_VideoFrame* f, const float* p, int32_t n) {
    if (!VideoValid(f)) return 1;
    const float strength =
        std::clamp(Param(p, n, 0, 0.55f), 0.f, 1.f);
    const float radius = std::clamp(Param(p, n, 1, 0.55f), 0.f, 1.f);
    const float feather =
        std::clamp(Param(p, n, 2, 0.45f), 0.01f, 1.f);
    const float aspect =
        std::clamp(Param(p, n, 3, 1.f), 0.5f, 2.f);
    if (strength <= 0.f) return 0;
    const float cx = f->width * 0.5f;
    const float cy = f->height * 0.5f;
    const float max_d = std::sqrt(cx * cx + cy * cy);
    if (max_d <= 0.f) return 0;
    const float outer = std::min(1.f, radius + feather);
    const auto smoothstep = [](float e0, float e1, float x) {
        const float d = e1 - e0;
        const float t = d != 0.f
                            ? std::clamp((x - e0) / d, 0.f, 1.f)
                            : (x < e0 ? 0.f : 1.f);
        return t * t * (3.f - 2.f * t);
    };
    VeParallelRows(f->height, [&](int32_t ya, int32_t yb) {
    for (int32_t y = ya; y < yb; ++y) {
        uint8_t* row = f->data + static_cast<size_t>(y) * f->stride;
        const float dy = ((y + 0.5f) - cy) * aspect;
        for (int32_t x = 0; x < f->width; ++x) {
            const float dx = (x + 0.5f) - cx;
            const float nd = std::sqrt(dx * dx + dy * dy) / max_d;
            const float factor =
                1.f - strength * smoothstep(radius, outer, nd);
            uint8_t* px = row + static_cast<size_t>(x) * 4;
            for (int32_t c = 0; c < 3; ++c) {
                px[c] = Clamp8f(px[c] * factor);
            }
        }
    }
    });
    return 0;
}

// Compositor dispatch surface (builtin_fx.h): the ids that take
// (media_time, clip_seed). halation/vignette2 are pure per-frame
// kernels — they route through the normal ABI path instead.
bool FilmEffectAppliesTo(const std::string& id) {
    return id == "xer.fx.film_grain" || id == "xer.fx.gate_weave" ||
           id == "xer.fx.light_leak" || id == "xer.fx.film_flicker" ||
           id == "xer.fx.dust_scratch";
}

int ApplyFilmKernel(const std::string& id, XER_VideoFrame* f,
                    const float* p, int32_t n, double media_time_s,
                    std::uint64_t clip_seed) {
    if (id == "xer.fx.film_grain") {
        return film_grain_kernel(f, p, n, media_time_s, clip_seed);
    }
    if (id == "xer.fx.gate_weave") {
        return gate_weave_kernel(f, p, n, media_time_s, clip_seed);
    }
    if (id == "xer.fx.light_leak") {
        return light_leak_kernel(f, p, n, media_time_s, clip_seed);
    }
    if (id == "xer.fx.film_flicker") {
        return film_flicker_kernel(f, p, n, media_time_s, clip_seed);
    }
    if (id == "xer.fx.dust_scratch") {
        return dust_scratch_kernel(f, p, n, media_time_s, clip_seed);
    }
    return 1;
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

// ── audio (effects_rack exposure wave) ─────────────────────────────
// The src/audio/effects_rack classes are mono-per-instance engines
// with real per-sample state (envelopes, delay lines, LFO phases).
// The ABI fn is stateless per call, so each fn (a) constructs its
// rack instance inside the call — state lives exactly one block, the
// same contract the ported echo/onepole kernels already keep — and
// (b) runs one instance PER CHANNEL via PerChannel below: flat
// interleaved processing would alternate L/R samples through a
// single envelope/delay line and corrupt stereo content.
//
// fn(float* mono, int32_t frames, int channel) -> bool ok
template <typename Fn>
int PerChannel(XER_AudioBuffer* b, Fn&& fn) {
    if (!AudioValid(b)) return 0;
    const int32_t ch = b->channels;
    const int32_t frames = b->frames;
    std::vector<std::vector<float>> lanes(static_cast<size_t>(ch));
    for (int32_t c = 0; c < ch; ++c) {
        lanes[static_cast<size_t>(c)].assign(
            static_cast<size_t>(frames), 0.f);
    }
    for (int32_t i = 0; i < frames; ++i) {
        float* frame = b->samples + static_cast<size_t>(i) * ch;
        for (int32_t c = 0; c < ch; ++c) {
            lanes[static_cast<size_t>(c)][static_cast<size_t>(i)] =
                frame[c];
        }
    }
    for (int32_t c = 0; c < ch; ++c) {
        if (!fn(lanes[static_cast<size_t>(c)].data(), frames, c)) {
            return 1;
        }
    }
    for (int32_t i = 0; i < frames; ++i) {
        float* frame = b->samples + static_cast<size_t>(i) * ch;
        for (int32_t c = 0; c < ch; ++c) {
            frame[c] = lanes[static_cast<size_t>(c)][static_cast<size_t>(i)];
        }
    }
    return 0;
}

namespace Rack = XER::Audio::FX;

// xer.af.compressor — DynamicsCompressor: threshold/ratio/attack/
// release, per-channel envelope.
int af_compressor(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float th = std::clamp(Param(p, n, 0, -18.f), -60.f, 0.f);
    const float ratio = std::clamp(Param(p, n, 1, 4.f), 1.f, 20.f);
    const float atk = std::clamp(Param(p, n, 2, 10.f), 0.1f, 500.f);
    const float rel = std::clamp(Param(p, n, 3, 120.f), 1.f, 2000.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::DynamicsCompressor comp;
        comp.SetSampleRate(b->sample_rate);
        comp.SetThresholdDb(th);
        comp.SetRatio(ratio);
        comp.SetAttackMs(atk);
        comp.SetReleaseMs(rel);
        return comp.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.limiter — DynamicsLimiter: ceiling + release. The class's
// release_ is a raw per-sample one-pole coefficient; the param is a
// friendlier release_ms mapped to that coefficient.
int af_limiter(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float ceil_db =
        std::clamp(Param(p, n, 0, -1.f), -20.f, 0.f);
    const float rel_ms =
        std::clamp(Param(p, n, 1, 50.f), 0.1f, 2000.f);
    const float coeff =
        1.f - std::exp(-1.f / (b->sample_rate * rel_ms * 0.001f));
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::DynamicsLimiter lim;
        lim.SetCeilingDb(ceil_db);
        lim.SetRelease(coeff);
        return lim.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.gate — DynamicsGate: threshold + hold time (ms -> samples).
int af_gate(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float th = std::clamp(Param(p, n, 0, -50.f), -80.f, 0.f);
    const float hold_ms =
        std::clamp(Param(p, n, 1, 50.f), 0.f, 2000.f);
    const size_t hold = static_cast<size_t>(
        hold_ms * 0.001f * b->sample_rate + 0.5f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::DynamicsGate gate;
        gate.SetThresholdDb(th);
        gate.SetHoldSamples(hold);
        return gate.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.expander — DynamicsExpander: threshold/ratio/range.
int af_expander(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float th = std::clamp(Param(p, n, 0, -40.f), -60.f, 0.f);
    const float ratio = std::clamp(Param(p, n, 1, 2.f), 1.f, 10.f);
    const float range = std::clamp(Param(p, n, 2, 60.f), 0.f, 80.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::DynamicsExpander ex;
        ex.SetThresholdDb(th);
        ex.SetRatio(ratio);
        ex.SetRangeDb(range);
        return ex.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.eq_parametric — ParametricEQ: 3 parallel band-pass bands,
// (freq, gain_db, q) triples. gain_db == 0 leaves the band a no-op.
int af_eq_parametric(XER_AudioBuffer* b, const float* p, int32_t n) {
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ParametricEQ eq;
        if (!eq.Initialize(b->sample_rate, 3)) return false;
        static constexpr float kDefaultFreq[3] = {200.f, 1000.f,
                                                  8000.f};
        for (int band = 0; band < 3; ++band) {
            Rack::ParametricBand desc{
                std::clamp(Param(p, n, band * 3 + 0,
                                 kDefaultFreq[band]),
                           20.f, 20000.f),
                std::clamp(Param(p, n, band * 3 + 2, 1.f), 0.2f, 10.f),
                std::clamp(Param(p, n, band * 3 + 1, 0.f), -18.f,
                           18.f),
                true};
            eq.SetBand(static_cast<size_t>(band), desc);
        }
        return eq.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.eq_graphic — GraphicEQ: fixed ISO octave centers, ten
// per-band gain params.
int af_eq_graphic(XER_AudioBuffer* b, const float* p, int32_t n) {
    static const std::vector<float> kCenters = {
        31.25f, 62.5f, 125.f, 250.f, 500.f,
        1000.f, 2000.f, 4000.f, 8000.f, 16000.f};
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::GraphicEQ eq;
        if (!eq.Initialize(b->sample_rate, kCenters)) return false;
        for (size_t band = 0; band < kCenters.size(); ++band) {
            eq.SetBandGainDb(
                band, std::clamp(
                          Param(p, n, static_cast<int32_t>(band), 0.f),
                          -18.f, 18.f));
        }
        return eq.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.chorus — ModChorus: rate/depth(ms)/mix over a 60 ms delay.
int af_chorus(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float rate = std::clamp(Param(p, n, 0, 0.8f), 0.05f, 10.f);
    const float depth_ms =
        std::clamp(Param(p, n, 1, 8.f), 0.f, 20.f);
    const float mix = std::clamp(Param(p, n, 2, 0.5f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ModChorus fx;
        if (!fx.Initialize(b->sample_rate,
                           static_cast<size_t>(b->sample_rate * 0.06f)))
            return false;
        fx.SetRateHz(rate);
        fx.SetDepthSamples(depth_ms * 0.001f * b->sample_rate);
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.flanger — ModFlanger: rate/depth/feedback/mix, 20 ms line.
int af_flanger(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float rate = std::clamp(Param(p, n, 0, 0.5f), 0.05f, 10.f);
    const float depth_ms =
        std::clamp(Param(p, n, 1, 2.f), 0.f, 10.f);
    const float fb = std::clamp(Param(p, n, 2, 0.5f), -0.95f, 0.95f);
    const float mix = std::clamp(Param(p, n, 3, 0.5f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ModFlanger fx;
        if (!fx.Initialize(b->sample_rate,
                           static_cast<size_t>(b->sample_rate * 0.02f)))
            return false;
        fx.SetRateHz(rate);
        fx.SetDepthSamples(depth_ms * 0.001f * b->sample_rate);
        fx.SetFeedback(fb);
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.phaser — ModPhaser: stages/rate/depth/feedback/mix.
int af_phaser(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float stages = std::clamp(Param(p, n, 0, 4.f), 1.f, 12.f);
    const float rate = std::clamp(Param(p, n, 1, 0.5f), 0.05f, 10.f);
    const float depth = std::clamp(Param(p, n, 2, 0.7f), 0.f, 1.f);
    const float fb = std::clamp(Param(p, n, 3, 0.4f), -0.95f, 0.95f);
    const float mix = std::clamp(Param(p, n, 4, 0.5f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ModPhaser fx;
        if (!fx.Initialize(b->sample_rate,
                           static_cast<size_t>(stages)))
            return false;
        fx.SetRateHz(rate);
        fx.SetDepth(depth);
        fx.SetFeedback(fb);
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.vibrato — ModVibrato (fx_mod_extra): rate/depth_ms/mix on a
// 30 ms fractional delay.
int af_vibrato(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float rate = std::clamp(Param(p, n, 0, 5.f), 0.05f, 20.f);
    const float depth_ms =
        std::clamp(Param(p, n, 1, 4.f), 0.f, 15.f);
    const float mix = std::clamp(Param(p, n, 2, 1.f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ModVibrato fx;
        if (!fx.Initialize(b->sample_rate,
                           static_cast<size_t>(b->sample_rate * 0.03f)))
            return false;
        fx.SetRateHz(rate);
        fx.SetDepthMs(depth_ms);
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.ring_mod — ModRingMod (fx_mod_extra): carrier freq + mix.
int af_ring_mod(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float freq =
        std::clamp(Param(p, n, 0, 80.f), 1.f, 2000.f);
    const float mix = std::clamp(Param(p, n, 1, 1.f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ModRingMod fx;
        if (!fx.Initialize(b->sample_rate)) return false;
        fx.SetFrequencyHz(freq);
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// xer.af.autotune — AutoTuneCorrectPitch: amount/retune/humanize/
// scale (root+mode)/presence/wet. ProcessBlock caps at
// max_block_size, so each channel runs in 4096-sample chunks.
int af_autotune(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float amount = std::clamp(Param(p, n, 0, 0.88f), 0.f, 1.f);
    const float retune =
        std::clamp(Param(p, n, 1, 18.f), 1.f, 400.f);
    const float humanize =
        std::clamp(Param(p, n, 2, 0.12f), 0.f, 1.f);
    const float root = std::clamp(Param(p, n, 3, 0.f), 0.f, 11.f);
    const float mode = std::clamp(Param(p, n, 4, 0.f), 0.f, 3.f);
    const float presence =
        std::clamp(Param(p, n, 5, 1.6f), -12.f, 12.f);
    const float wet = std::clamp(Param(p, n, 6, 0.92f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::AutoTuneCorrectPitch fx;
        constexpr size_t kChunk = 4096;
        if (!fx.Initialize(b->sample_rate, kChunk)) return false;
        fx.SetCorrectionAmount(amount);
        fx.SetRetuneSpeedMs(retune);
        fx.SetHumanize(humanize);
        fx.SetScaleRoot(root);
        fx.SetScaleMode(mode);
        fx.SetPresenceDb(presence);
        fx.SetWetMix(wet);
        for (int32_t off = 0; off < frames; off += kChunk) {
            const size_t count = static_cast<size_t>(
                std::min<int32_t>(frames - off, kChunk));
            if (!fx.ProcessBlock(s + off, count, s + off)) return false;
        }
        return true;
    });
}

// xer.af.reverb — ReverbAlgorithmic: room_size/damping/mix.
int af_reverb(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float room = std::clamp(Param(p, n, 0, 0.6f), 0.f, 1.f);
    const float damp = std::clamp(Param(p, n, 1, 0.5f), 0.f, 1.f);
    const float mix = std::clamp(Param(p, n, 2, 0.3f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int) {
        Rack::ReverbAlgorithmic fx;
        // Longest internal tap scales to ~140 ms at room_size 1 —
        // 250 ms of line covers every setting.
        if (!fx.Initialize(
                b->sample_rate,
                static_cast<size_t>(b->sample_rate * 0.25f)))
            return false;
        fx.SetRoomSize(room);
        fx.SetDamping(damp);
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
}

// Procedural impulse response for xer.af.reverb_ir: exponentially
// decaying white noise (-60 dB at the tail) with a progressive
// one-pole HF rolloff — `damp` sweeps the coefficient from bright
// to dark along the tail. Per-channel seeds decorrelate L/R so the
// space isn't perfectly mono.
std::vector<float> MakeImpulseResponse(float sample_rate,
                                       float decay_s, float damp,
                                       std::uint32_t seed) {
    const size_t len = std::max<size_t>(
        64, static_cast<size_t>(decay_s * sample_rate + 0.5f));
    std::vector<float> ir(len);
    std::uint32_t rng = seed;
    float lp = 0.f;
    for (size_t i = 0; i < len; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float white =
            static_cast<float>((rng >> 9) & 0x7FFFFFu) / 4194304.f -
            1.f;
        const float t = static_cast<float>(i) / len;
        // a sweeps 0.95 -> 0.03 as damp*time rises (damp 0 = flat).
        const float a =
            std::clamp(0.95f - 0.92f * damp * t, 0.01f, 1.f);
        lp += a * (white - lp);
        ir[i] = lp * std::exp(-6.9078f * t);
    }
    return ir;
}

// xer.af.reverb_ir — ReverbConvolution: decay/damp/mix against a
// synthesized IR (the ABI has no string/file param channel, so the
// IR is generated rather than loaded — deterministic and
// license-free). Per-channel seeds give a stereo-correlated but not
// identical tail.
int af_reverb_ir(XER_AudioBuffer* b, const float* p, int32_t n) {
    const float decay =
        std::clamp(Param(p, n, 0, 1.2f), 0.05f, 6.f);
    const float damp = std::clamp(Param(p, n, 1, 0.4f), 0.f, 1.f);
    const float mix = std::clamp(Param(p, n, 2, 0.3f), 0.f, 1.f);
    return PerChannel(b, [&](float* s, int32_t frames, int c) {
        Rack::ReverbConvolution fx;
        const std::vector<float> ir = MakeImpulseResponse(
            b->sample_rate, decay, damp,
            0x9E3779B9u + static_cast<std::uint32_t>(c) * 0x85EBCA6Bu);
        if (!fx.LoadImpulseResponse(ir.data(), ir.size())) {
            return false;
        }
        fx.SetMix(mix);
        return fx.ProcessBlock(s, static_cast<size_t>(frames), s);
    });
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
        // Film Grain v2 — same id/leading params as the retired DLL
        // (amount 0..100, mono 0/1) so serialized effects keep their
        // meaning; size/seed extend positionally. Compositor-
        // dispatched for the animated (clip,time)-seeded field — the
        // ABI fn renders the static t=0 variant.
        [] {
            PluginDesc d =
                Desc("xer.fx.film_grain", "Film Grain",
                     XER_PLUGIN_VIDEO_FX, &film_grain, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"amount", 0.0f, 100.0f, 20.0f},
                {"mono", 0.0f, 1.0f, 1.0f},
                {"size", 1.0f, 8.0f, 1.0f},
                {"seed", 0.0f, 999.0f, 0.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
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
        // ── video (film look — research 2026-11-24 §1.1) ──
        // Halation and Vignette 2 are pure per-frame kernels (same
        // contract as the rest of this table). The other four are
        // time-aware like film_grain: compositor-dispatched via
        // BuiltinFx::ApplyFilmEffect, ABI fn renders the static
        // variant.
        [] {
            PluginDesc d =
                Desc("xer.fx.halation", "Halation",
                     XER_PLUGIN_VIDEO_FX, &halation, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"threshold", 0.0f, 255.0f, 200.0f},
                {"spread", 1.0f, 64.0f, 14.0f},
                {"strength", 0.0f, 1.0f, 0.5f},
                {"hue", 0.0f, 360.0f, 25.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.fx.gate_weave", "Gate Weave",
                     XER_PLUGIN_VIDEO_FX, &gate_weave, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"amplitude", 0.0f, 16.0f, 1.5f},
                {"speed", 0.01f, 8.0f, 0.6f},
                {"seed", 0.0f, 999.0f, 0.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.fx.light_leak", "Light Leak",
                     XER_PLUGIN_VIDEO_FX, &light_leak, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"intensity", 0.0f, 1.0f, 0.45f},
                {"hue", 0.0f, 360.0f, 30.0f},
                {"seed", 0.0f, 999.0f, 0.0f},
                {"animated", 0.0f, 1.0f, 1.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.fx.film_flicker", "Film Flicker",
                     XER_PLUGIN_VIDEO_FX, &film_flicker, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"amount", 0.0f, 1.0f, 0.18f},
                {"rate", 0.5f, 24.0f, 6.0f},
                {"seed", 0.0f, 999.0f, 0.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.fx.dust_scratch", "Dust & Scratch",
                     XER_PLUGIN_VIDEO_FX, &dust_scratch, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"density", 0.0f, 1.0f, 0.35f},
                {"seed", 0.0f, 999.0f, 0.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.fx.vignette2", "Vignette 2",
                     XER_PLUGIN_VIDEO_FX, &vignette2, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"strength", 0.0f, 1.0f, 0.55f},
                {"radius", 0.0f, 1.0f, 0.55f},
                {"feather", 0.01f, 1.0f, 0.45f},
                {"aspect", 0.5f, 2.0f, 1.0f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
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
        // ── video (Tracked Mosaic — track-driven, compositor) ──
        // Pin-an-effect target for Clip::object_track ("Track object
        // → Pin → Mosaic", the auto-censor path). Same dispatch shape
        // as Smart Reframe: the box it pixelates comes from the clip's
        // analyzed track at the frame's source time — no static param
        // can express it, so compositor.cc resolves the region and
        // skips the ABI fn (identity here keeps generic paths inert).
        [] {
            PluginDesc d =
                Desc("xer.fx.track_mosaic", "Tracked Mosaic",
                     XER_PLUGIN_VIDEO_FX, &smart_reframe_noop, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"block", 2.0f, 128.0f, 12.0f},   // pixel-block px
                {"padding", 0.0f, 1.0f, 0.15f},   // grow box, frac of it
                {"smoothing", 0.0f, 1.0f, 0.5f},  // track temporal smoothing
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        // ── video (Face Blur — YuNet track-driven, compositor) ──
        // The privacy-named sibling of Tracked Mosaic: "Auto-blur
        // faces" (ctx menu) scans the clip with YuNet into
        // Clip::object_track label "face" and applies this effect.
        // Same dispatch contract — the compositor resolves the
        // tracked box per frame; the ABI fn stays identity. Params
        // default a finer block + a touch more padding than the
        // generic mosaic since faces are the privacy target.
        [] {
            PluginDesc d =
                Desc("xer.fx.face_blur", "Face Blur (AI)",
                     XER_PLUGIN_VIDEO_FX, &smart_reframe_noop, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"block", 2.0f, 128.0f, 16.0f},   // pixel-block px
                {"padding", 0.0f, 1.0f, 0.25f},   // grow box, frac of it
                {"smoothing", 0.0f, 1.0f, 0.5f},  // track temporal smoothing
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        // ── video (Track Mask — SAM2.1 mask-driven, compositor) ──
        // The mask-native sibling of Tracked Mosaic: "Track Mask (AI)"
        // (clip ctx menu → click the subject) runs SAM2.1-hiera-tiny
        // per frame into Clip::mask_track (packed-bit silhouette
        // grids), and this effect resolves that mask at the frame's
        // source time. Same compositor-dispatch contract as the other
        // track effects — the ABI fn stays identity. Modes: isolate
        // (alpha out everything but the subject), blur_bg (defocus the
        // background plate), key (emit the matte as alpha).
        [] {
            PluginDesc d =
                Desc("xer.fx.track_mask", "Track Mask (AI)",
                     XER_PLUGIN_VIDEO_FX, &smart_reframe_noop, nullptr);
            static const XER_ParamDesc kParams[] = {
                {"mode", 0.0f, 2.0f, 0.0f},  // 0 isolate|1 blur_bg|2 key
                {"feather", 0.0f, 1.0f, 0.1f},  // soft edge, frac of grid
                {"smoothing", 0.0f, 1.0f, 0.5f},  // track temporal smoothing
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
        // ── audio (effects_rack exposure — src/audio/effects_rack) ──
        // Dynamics. Per-channel instances inside the ABI fn (see the
        // wave comment above PerChannel).
        [] {
            PluginDesc d =
                Desc("xer.af.compressor", "Compressor",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_compressor);
            static const XER_ParamDesc kParams[] = {
                {"threshold_db", -60.f, 0.f, -18.f},
                {"ratio", 1.f, 20.f, 4.f},
                {"attack_ms", 0.1f, 500.f, 10.f},
                {"release_ms", 1.f, 2000.f, 120.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.limiter", "Limiter",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_limiter);
            static const XER_ParamDesc kParams[] = {
                {"ceiling_db", -20.f, 0.f, -1.f},
                {"release_ms", 0.1f, 2000.f, 50.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d = Desc("xer.af.gate", "Noise Gate",
                                XER_PLUGIN_AUDIO_FX, nullptr, &af_gate);
            static const XER_ParamDesc kParams[] = {
                {"threshold_db", -80.f, 0.f, -50.f},
                {"hold_ms", 0.f, 2000.f, 50.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.expander", "Expander",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_expander);
            static const XER_ParamDesc kParams[] = {
                {"threshold_db", -60.f, 0.f, -40.f},
                {"ratio", 1.f, 10.f, 2.f},
                {"range_db", 0.f, 80.f, 60.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        // EQ. Parametric is 3 (freq, gain, q) triples; graphic is a
        // fixed 10-band ISO set of gains.
        [] {
            PluginDesc d =
                Desc("xer.af.eq_parametric", "Parametric EQ",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_eq_parametric);
            static const XER_ParamDesc kParams[] = {
                {"freq_1", 20.f, 20000.f, 200.f},
                {"gain_1", -18.f, 18.f, 0.f},
                {"q_1", 0.2f, 10.f, 1.f},
                {"freq_2", 20.f, 20000.f, 1000.f},
                {"gain_2", -18.f, 18.f, 0.f},
                {"q_2", 0.2f, 10.f, 1.f},
                {"freq_3", 20.f, 20000.f, 8000.f},
                {"gain_3", -18.f, 18.f, 0.f},
                {"q_3", 0.2f, 10.f, 1.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.eq_graphic", "Graphic EQ (10-band)",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_eq_graphic);
            static const XER_ParamDesc kParams[] = {
                {"31_hz", -18.f, 18.f, 0.f},
                {"62_hz", -18.f, 18.f, 0.f},
                {"125_hz", -18.f, 18.f, 0.f},
                {"250_hz", -18.f, 18.f, 0.f},
                {"500_hz", -18.f, 18.f, 0.f},
                {"1k_hz", -18.f, 18.f, 0.f},
                {"2k_hz", -18.f, 18.f, 0.f},
                {"4k_hz", -18.f, 18.f, 0.f},
                {"8k_hz", -18.f, 18.f, 0.f},
                {"16k_hz", -18.f, 18.f, 0.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        // Modulation.
        [] {
            PluginDesc d =
                Desc("xer.af.chorus", "Chorus",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_chorus);
            static const XER_ParamDesc kParams[] = {
                {"rate_hz", 0.05f, 10.f, 0.8f},
                {"depth_ms", 0.f, 20.f, 8.f},
                {"mix", 0.f, 1.f, 0.5f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.flanger", "Flanger",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_flanger);
            static const XER_ParamDesc kParams[] = {
                {"rate_hz", 0.05f, 10.f, 0.5f},
                {"depth_ms", 0.f, 10.f, 2.f},
                {"feedback", -0.95f, 0.95f, 0.5f},
                {"mix", 0.f, 1.f, 0.5f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.phaser", "Phaser",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_phaser);
            static const XER_ParamDesc kParams[] = {
                {"stages", 1.f, 12.f, 4.f},
                {"rate_hz", 0.05f, 10.f, 0.5f},
                {"depth", 0.f, 1.f, 0.7f},
                {"feedback", -0.95f, 0.95f, 0.4f},
                {"mix", 0.f, 1.f, 0.5f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.vibrato", "Vibrato",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_vibrato);
            static const XER_ParamDesc kParams[] = {
                {"rate_hz", 0.05f, 20.f, 5.f},
                {"depth_ms", 0.f, 15.f, 4.f},
                {"mix", 0.f, 1.f, 1.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.ring_mod", "Ring Modulator",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_ring_mod);
            static const XER_ParamDesc kParams[] = {
                {"freq_hz", 1.f, 2000.f, 80.f},
                {"mix", 0.f, 1.f, 1.f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        // Pitch + space.
        [] {
            PluginDesc d =
                Desc("xer.af.autotune", "Auto-Tune (Pitch Correct)",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_autotune);
            static const XER_ParamDesc kParams[] = {
                {"amount", 0.f, 1.f, 0.88f},
                {"retune_ms", 1.f, 400.f, 18.f},
                {"humanize", 0.f, 1.f, 0.12f},
                {"root", 0.f, 11.f, 0.f},   // 0=C
                {"scale", 0.f, 3.f, 0.f},   // 0 chromatic
                {"presence_db", -12.f, 12.f, 1.6f},
                {"wet", 0.f, 1.f, 0.92f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.reverb", "Reverb",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_reverb);
            static const XER_ParamDesc kParams[] = {
                {"room_size", 0.f, 1.f, 0.6f},
                {"damping", 0.f, 1.f, 0.5f},
                {"mix", 0.f, 1.f, 0.3f},
            };
            d.params.assign(std::begin(kParams), std::end(kParams));
            return d;
        }(),
        [] {
            PluginDesc d =
                Desc("xer.af.reverb_ir", "Reverb IR (Convolution)",
                     XER_PLUGIN_AUDIO_FX, nullptr, &af_reverb_ir);
            static const XER_ParamDesc kParams[] = {
                {"decay_s", 0.05f, 6.f, 1.2f},
                {"damping", 0.f, 1.f, 0.4f},
                {"mix", 0.f, 1.f, 0.3f},
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

bool IsFilmEffectId(const std::string& id) {
    return FilmEffectAppliesTo(id);
}

bool ApplyFilmEffect(const std::string& id, void* bgra, int width,
                     int height, int stride,
                     const std::vector<float>& params,
                     double media_time_s, std::uint64_t clip_seed) {
    if (bgra == nullptr) return false;
    XER_VideoFrame frame{static_cast<std::uint8_t*>(bgra), width,
                         height, stride};
    return ApplyFilmKernel(id, &frame, params.data(),
                           static_cast<std::int32_t>(params.size()),
                           media_time_s, clip_seed) == 0;
}

}  // namespace XER::VideoEditor::BuiltinFx
