// transitions_lib.cc — see transitions_lib.h for the contract.
// All kernels run on packed BGRA8 straight-alpha canvas frames.
// Coordinates: uv in [0,1]x[0,1] with pixel CENTER convention
// ((x+0.5)/W, (y+0.5)/H), y grows DOWN the frame (screen space).
//
// Provenance (full audit: docs/wiki/research/2026-11-24-transitions.md):
//   whip_pan        port of GL-transitions WhipPan.glsl  (gre, MIT)
//   zoom            port of CrossZoom.glsl   (rectalogic, MIT)
//   morph           port of Morph.glsl       (paniq, MIT)
//   directional_wipe port of DirectionalWipe.glsl (gre, MIT)
//   iris            port of CircleOpen.glsl  (gre, MIT) + shapes/close
//   spin            port of WhipSpin.glsl    (gre, MIT)
//   pixel_displace  RandomSquares.glsl       (gre, MIT) + displacement
//   ink             PerlinDissolve.glsl      (Rich Harris, MIT)
//   film_roll       simplified FilmStrip.glsl (gre, MIT)
//   luma_fade/glitch/chroma/light_leak/page_curl/flash — original
//   (page_curl deliberately NOT the HP BSD-3 shader; it's an
//   independent diagonal shadowed-roll approximation.)
#include "transitions_lib.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace XER::VideoEditor {
namespace {

// ------------------------------------------------------------------
// Small helpers — packed BGRA8 straight alpha (B,G,R,A byte order).
// ------------------------------------------------------------------

inline std::uint8_t Clamp8(double v) {
    return static_cast<std::uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
}

inline double Smoothstep(double lo, double hi, double x) {
    // Reversed edges (hi < lo) are legal in the GL-transitions idiom
    // (smoothstep(0,-s,x) fades out over a soft band) — equal to
    // 1 - smoothstep(hi, lo, x).
    if (hi < lo) {
        return 1.0 - Smoothstep(hi, lo, x);
    }
    if (!(hi > lo)) {
        return x >= hi ? 1.0 : 0.0;
    }
    const double t = std::clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Deterministic integer-lattice hash -> [0,1). All noise/jitter in
// the kernels derives from this + params.seed, so output is a pure
// function of the inputs (no wall time, no state).
inline std::uint32_t HashU32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline double Hash01(int x, int y, std::uint32_t seed) {
    const std::uint32_t h =
        seed ^ (static_cast<std::uint32_t>(x) * 0x9E3779B1U) ^
        (static_cast<std::uint32_t>(y) * 0x85EBCA77U);
    return static_cast<double>(HashU32(h) >> 8) * (1.0 / 16777216.0);
}

// Bilinear sample at normalized uv (center convention), edge-clamped.
inline void SampleUV(const std::uint8_t* px, int w, int h,
                     double u, double v, std::uint8_t* out) {
    const double fx = u * w - 0.5;
    const double fy = v * h - 0.5;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const double tx = fx - x0;
    const double ty = fy - y0;
    auto tap = [&](int x, int y) -> const std::uint8_t* {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px + (static_cast<std::size_t>(y) * w + x) * 4;
    };
    const std::uint8_t* p00 = tap(x0, y0);
    const std::uint8_t* p10 = tap(x0 + 1, y0);
    const std::uint8_t* p01 = tap(x0, y0 + 1);
    const std::uint8_t* p11 = tap(x0 + 1, y0 + 1);
    const double w00 = (1.0 - tx) * (1.0 - ty);
    const double w10 = tx * (1.0 - ty);
    const double w01 = (1.0 - tx) * ty;
    const double w11 = tx * ty;
    for (int c = 0; c < 4; ++c) {
        out[c] = Clamp8(p00[c] * w00 + p10[c] * w10 + p01[c] * w01 +
                        p11[c] * w11);
    }
}

// Nearest sample at normalized uv, edge-clamped (glitch hard edges).
inline const std::uint8_t* SampleUVNear(const std::uint8_t* px, int w,
                                        int h, double u, double v) {
    const int x = std::clamp(static_cast<int>(u * w), 0, w - 1);
    const int y = std::clamp(static_cast<int>(v * h), 0, h - 1);
    return px + (static_cast<std::size_t>(y) * w + x) * 4;
}

// Premultiplied-alpha lerp of two pixels (the compositor's crossfade
// convention — correct for straight-alpha layers, equals a plain
// lerp when both are opaque).
inline void MixPix(const std::uint8_t* u, const std::uint8_t* o,
                   double w, std::uint8_t* d) {
    const double ua = u[3] / 255.0;
    const double oa = o[3] / 255.0;
    const double pa = ua * (1.0 - w) + oa * w;
    if (pa <= 1e-9) {
        d[0] = d[1] = d[2] = d[3] = 0;
        return;
    }
    for (int c = 0; c < 3; ++c) {
        d[c] = Clamp8((u[c] * ua * (1.0 - w) + o[c] * oa * w) / pa);
    }
    d[3] = Clamp8(pa * 255.0);
}

// Reveal composite: `over` alpha-scaled by coverage `m` over `under`.
inline void RevealPix(const std::uint8_t* u, const std::uint8_t* o,
                      double m, std::uint8_t* d) {
    if (m <= 0.0) {
        d[0] = u[0]; d[1] = u[1]; d[2] = u[2]; d[3] = u[3];
        return;
    }
    if (m >= 1.0) {
        d[0] = o[0]; d[1] = o[1]; d[2] = o[2]; d[3] = o[3];
        return;
    }
    const double sa = (o[3] / 255.0) * m;
    const double da = u[3] / 255.0;
    const double outa = sa + da * (1.0 - sa);
    if (outa <= 1e-9) {
        d[0] = d[1] = d[2] = d[3] = 0;
        return;
    }
    for (int c = 0; c < 3; ++c) {
        d[c] = Clamp8((o[c] * sa + u[c] * da * (1.0 - sa)) / outa);
    }
    d[3] = Clamp8(outa * 255.0);
}

// Compass vector for direction params: 0..7 -> E,SE,S,SW,W,NW,N,NE
// in screen space (y down). L1-normalized like GL DirectionalWipe so
// diagonals sweep the same fraction range as axes.
inline void CompassVec(int dir, double* vx, double* vy) {
    static const double kDiag = 0.7071067811865476;
    static const double kVec[8][2] = {
        {1, 0}, {kDiag, kDiag}, {0, 1}, {-kDiag, kDiag},
        {-1, 0}, {-kDiag, -kDiag}, {0, -1}, {kDiag, -kDiag},
    };
    const int i = ((dir % 8) + 8) % 8;
    *vx = kVec[i][0];
    *vy = kVec[i][1];
}

// 2D value noise (PerlinDissolve's lattice+smoothstep form) built on
// Hash01 — returns [0,1).
double ValueNoise(double u, double v, std::uint32_t seed) {
    const int ix = static_cast<int>(std::floor(u));
    const int iy = static_cast<int>(std::floor(v));
    const double fx = u - ix;
    const double fy = v - iy;
    const double sx = fx * fx * (3.0 - 2.0 * fx);
    const double sy = fy * fy * (3.0 - 2.0 * fy);
    const double a = Hash01(ix, iy, seed);
    const double b = Hash01(ix + 1, iy, seed);
    const double c = Hash01(ix, iy + 1, seed);
    const double d = Hash01(ix + 1, iy + 1, seed);
    return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}

using KernelFn = void (*)(const XerTransitionParams&, double w,
                          const std::uint8_t* u, const std::uint8_t* o,
                          std::uint8_t* d, int W, int H);

// ------------------------------------------------------------------
// xer.tr.whip_pan — fast pan + directional motion blur
// (port of WhipPan.glsl). dir: 0=left 1=right 2=up 3=down is the
// direction the IMAGES travel; int scales blur length.
// ------------------------------------------------------------------
void WhipPan(const XerTransitionParams& p, double w,
             const std::uint8_t* u, const std::uint8_t* o,
             std::uint8_t* d, int W, int H) {
    double vx = -1.0, vy = 0.0;   // default: pan left
    switch (((p.direction % 4) + 4) % 4) {
        case 0: vx = -1; vy = 0; break;
        case 1: vx = 1;  vy = 0; break;
        case 2: vx = 0;  vy = -1; break;
        case 3: vx = 0;  vy = 1; break;
    }
    const double h = 1.0 - std::fabs(2.0 * w - 1.0);   // 0,peak,0
    const double e = 0.5 * h * (0.1 + 0.9 * h * h);
    const double pos = w < 0.5 ? e : 1.0 - e;
    const double blur = 0.4 * std::clamp(p.intensity, 0.0, 1.0);
    const double len = blur * h * (0.1 + 2.7 * h * h) / 2.8;
    constexpr int kSamples = 14;
    const std::uint32_t seed = p.seed ^ 0x77A1u;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            const double qx = uu - vx * pos;
            const double qy = vv - vy * pos;
            const double jitter = Hash01(x, y, seed);
            double acc[4] = {0, 0, 0, 0};
            for (int i = 0; i < kSamples; ++i) {
                const double t =
                    (i + jitter) / kSamples - 0.5;
                const double sx = qx + vx * t * len;
                const double sy = qy + vy * t * len;
                // strip(): seam plane through 0.5 perpendicular to v;
                // incoming sits `v` behind the outgoing image.
                const bool under_side =
                    (sx - 0.5) * -vx + (sy - 0.5) * -vy <
                    0.5 * (vx * vx + vy * vy);
                std::uint8_t c[4];
                if (under_side) {
                    SampleUV(u, W, H, sx, sy, c);
                } else {
                    SampleUV(o, W, H, sx + vx, sy + vy, c);
                }
                const double a = c[3] / 255.0;
                acc[0] += c[0] * a;
                acc[1] += c[1] * a;
                acc[2] += c[2] * a;
                acc[3] += a;
            }
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            const double denom = std::max(acc[3], 1e-5);
            dp[0] = Clamp8(acc[0] / denom);
            dp[1] = Clamp8(acc[1] / denom);
            dp[2] = Clamp8(acc[2] / denom);
            dp[3] = Clamp8(acc[3] / kSamples * 255.0);
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.zoom — crossfade through a zoom blur (CrossZoom.glsl port).
// int scales blur strength; the focal point eases 0.25 -> 0.75 like
// the GL original (subtle lateral pull).
// ------------------------------------------------------------------
void Zoom(const XerTransitionParams& p, double w,
          const std::uint8_t* u, const std::uint8_t* o,
          std::uint8_t* d, int W, int H) {
    // Exponential easeInOut dissolve + sinusoidal strength loop.
    double dissolve;
    if (w <= 0.0) {
        dissolve = 0.0;
    } else if (w >= 1.0) {
        dissolve = 1.0;
    } else if (w < 0.5) {
        dissolve = 0.5 * std::pow(2.0, 20.0 * w - 10.0);
    } else {
        dissolve = 0.5 * (-std::pow(2.0, 10.0 - 20.0 * w) + 2.0);
    }
    const double strength =
        0.4 * std::clamp(p.intensity, 0.0, 1.0) *
        (-0.5 * (std::cos(3.141592653589793 * w / 0.5) - 1.0));
    const double cx = 0.25 + 0.5 * w;   // Linear ease 0.25->0.75
    const double cy = 0.5;
    constexpr int kTaps = 16;
    const std::uint32_t seed = p.seed ^ 0xC0FFu;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            const double tcx = cx - uu;
            const double tcy = cy - vv;
            const double offset = Hash01(x, y, seed);
            double acc[4] = {0, 0, 0, 0};
            double total = 0.0;
            for (int i = 0; i < kTaps; ++i) {
                const double pct = (i + offset) / kTaps;
                const double wt = 4.0 * (pct - pct * pct);
                const double su = uu + tcx * pct * strength;
                const double sv = vv + tcy * pct * strength;
                std::uint8_t ca[4], cb[4], m[4];
                SampleUV(u, W, H, su, sv, ca);
                SampleUV(o, W, H, su, sv, cb);
                MixPix(ca, cb, dissolve, m);
                for (int c = 0; c < 4; ++c) acc[c] += m[c] * wt;
                total += wt;
            }
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            for (int c = 0; c < 4; ++c) dp[c] = Clamp8(acc[c] / total);
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.luma_fade — luminance-ordered dissolve (Resolve convention):
// a luminance threshold sweeps; dark regions of the OUTGOING frame
// reveal the incoming first (dir=1 flips to bright-first). soft is
// the luma edge width.
// ------------------------------------------------------------------
void LumaFade(const XerTransitionParams& p, double w,
              const std::uint8_t* u, const std::uint8_t* o,
              std::uint8_t* d, int W, int H) {
    const double soft = std::max(p.softness, 0.001);
    const bool bright_first = ((p.direction % 2) + 2) % 2 == 1;
    const double wp = w * (1.0 + 2.0 * soft) - soft;   // [-s, 1+s]
    const int count = W * H;
    for (int i = 0; i < count; ++i) {
        const std::uint8_t* up = u + static_cast<std::size_t>(i) * 4;
        double l = (0.114 * up[0] + 0.587 * up[1] + 0.299 * up[2]) / 255.0;
        if (bright_first) l = 1.0 - l;
        const double m = Smoothstep(-soft, soft, wp - l);
        RevealPix(up, o + static_cast<std::size_t>(i) * 4, m,
                  d + static_cast<std::size_t>(i) * 4);
    }
}

// ------------------------------------------------------------------
// xer.tr.morph — color-driven displacement crossfade (Morph.glsl
// port): each image's (r+b, g+b) vector displaces the other side's
// sample position. int scales the 0.1 GL strength.
// ------------------------------------------------------------------
void Morph(const XerTransitionParams& p, double w,
           const std::uint8_t* u, const std::uint8_t* o,
           std::uint8_t* d, int W, int H) {
    const double s = 0.1 * std::clamp(p.intensity, 0.0, 2.0);
    const double w0 = w;
    const double w1 = 1.0 - w;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            std::uint8_t ca[4], cb[4];
            SampleUV(u, W, H, uu, vv, ca);
            SampleUV(o, W, H, uu, vv, cb);
            const double oax = (ca[2] / 255.0 + ca[0] / 255.0) - 1.0;
            const double oay = (ca[1] / 255.0 + ca[0] / 255.0) - 1.0;
            const double obx = (cb[2] / 255.0 + cb[0] / 255.0) - 1.0;
            const double oby = (cb[1] / 255.0 + cb[0] / 255.0) - 1.0;
            const double ocx = (oax + obx) * 0.5 * s;
            const double ocy = (oay + oby) * 0.5 * s;
            std::uint8_t sa[4], sb[4], m[4];
            SampleUV(u, W, H, uu + ocx * w0, vv + ocy * w0, sa);
            SampleUV(o, W, H, uu - ocx * w1, vv - ocy * w1, sb);
            MixPix(sa, sb, w, m);
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            dp[0] = m[0]; dp[1] = m[1]; dp[2] = m[2]; dp[3] = m[3];
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.glitch — slice jitter + per-block early/late reveal + RGB
// split + occasional desaturated block (GlitchDisplace/GlitchMemories
// derived, original arrangement). int scales displacement; the noise
// field is quantized to 1/8 progress steps so it strobes
// deterministically instead of shimmering per-sample.
// ------------------------------------------------------------------
void Glitch(const XerTransitionParams& p, double w,
            const std::uint8_t* u, const std::uint8_t* o,
            std::uint8_t* d, int W, int H) {
    const double inten = std::clamp(p.intensity, 0.0, 1.0);
    const double env = std::sin(3.141592653589793 * w);
    const int quant = static_cast<int>(w * 8.0 + 0.5);
    const std::uint32_t qs = p.seed ^ (static_cast<std::uint32_t>(quant) *
                                       7919u);
    const int band_h = std::max(4, H / 9);
    const int block = std::max(8, std::min(W, H) / 10);
    const double max_shift = 0.06 * inten;            // uv units
    const double split = 8.0 * inten * env / W;       // px->uv
    for (int y = 0; y < H; ++y) {
        const int band = y / band_h;
        const double dx =
            (Hash01(band, quant, qs) - 0.5) * 2.0 * max_shift * env;
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            const int bx = x / block;
            const int by = y / block;
            // Per-block progress jitter: blocks flip early/late.
            const double wb = std::clamp(
                w + (Hash01(bx, by, qs ^ 0x5D1u) - 0.5) * 0.45 * env,
                0.0, 1.0);
            const std::uint8_t* up =
                SampleUVNear(u, W, H, uu + dx, vv);
            const std::uint8_t* op =
                SampleUVNear(o, W, H, uu - dx, vv);
            std::uint8_t m[4];
            MixPix(up, op, wb, m);
            if (split > 0.0) {
                const std::uint8_t* ur =
                    SampleUVNear(u, W, H, uu + dx + split, vv);
                const std::uint8_t* orr =
                    SampleUVNear(o, W, H, uu - dx + split, vv);
                const std::uint8_t* ub =
                    SampleUVNear(u, W, H, uu + dx - split, vv);
                const std::uint8_t* obb =
                    SampleUVNear(o, W, H, uu - dx - split, vv);
                std::uint8_t mr[4], mb[4];
                MixPix(ur, orr, wb, mr);
                MixPix(ub, obb, wb, mb);
                m[2] = mr[2];   // R channel split one way
                m[0] = mb[0];   // B the other
            }
            // A few blocks collapse to gray mid-glitch (memories look).
            if (Hash01(bx, by, qs ^ 0x9E3u) < 0.15 * env) {
                const int g = (m[0] * 114 + m[1] * 587 + m[2] * 299) / 1000;
                m[0] = m[1] = m[2] = static_cast<std::uint8_t>(g);
            }
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            dp[0] = m[0]; dp[1] = m[1]; dp[2] = m[2]; dp[3] = m[3];
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.chroma — radial chromatic aberration through the cut: each
// frame's channels split radially (R leads, B trails), peaking at
// mid-transition (WhipSpin's per-tap chroma idea as a standalone).
// ------------------------------------------------------------------
void Chroma(const XerTransitionParams& p, double w,
            const std::uint8_t* u, const std::uint8_t* o,
            std::uint8_t* d, int W, int H) {
    const double env = std::sin(3.141592653589793 * w);
    const double k = 0.02 * std::clamp(p.intensity, 0.0, 1.0) * env;
    const double aspect = static_cast<double>(W) / std::max(H, 1);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            double nx = (uu - 0.5) * aspect;
            double ny = vv - 0.5;
            const double r = std::sqrt(nx * nx + ny * ny);
            if (r > 1e-6) {
                nx /= r;
                ny /= r;
            }
            const double ox = nx * r * k;   // radial offset, uv units
            const double oy = ny * r * k;
            std::uint8_t ua_r[4], ua_b[4], ua_g[4];
            std::uint8_t ob_r[4], ob_b[4], ob_g[4];
            SampleUV(u, W, H, uu + ox, vv + oy, ua_r);
            SampleUV(u, W, H, uu, vv, ua_g);
            SampleUV(u, W, H, uu - ox, vv - oy, ua_b);
            SampleUV(o, W, H, uu + ox, vv + oy, ob_r);
            SampleUV(o, W, H, uu, vv, ob_g);
            SampleUV(o, W, H, uu - ox, vv - oy, ob_b);
            std::uint8_t mp[4], mm[4], mn[4];
            MixPix(ua_r, ob_r, w, mp);   // B channel source (idx 0)
            MixPix(ua_g, ob_g, w, mm);   // G + alpha
            MixPix(ua_b, ob_b, w, mn);   // R channel source (idx 2)
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            dp[0] = mn[0];
            dp[1] = mm[1];
            dp[2] = mp[2];
            dp[3] = mm[3];
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.light_leak — warm leak sweeps across the frame covering the
// cut (film-kit tie-in). dir picks the sweep direction (compass),
// a second seeded offset blob adds the classic double-lobe shape.
// ------------------------------------------------------------------
void LightLeak(const XerTransitionParams& p, double w,
               const std::uint8_t* u, const std::uint8_t* o,
               std::uint8_t* d, int W, int H) {
    const double inten = std::clamp(p.intensity, 0.0, 1.0);
    double vx, vy;
    CompassVec(p.direction, &vx, &vy);
    const double nrm = std::fabs(vx) + std::fabs(vy);
    vx /= nrm;
    vy /= nrm;
    // Sweep position: the leak crosses the frame during the cut.
    const double c = -0.35 + 1.7 * w;
    const double c2 = 0.75 + 0.5 * (Hash01(3, 7, p.seed) - 0.5) +
                      0.25 * w;   // companion blob
    const double w_mix = Smoothstep(0.15, 0.85, w);
    const double width = 0.22 + 0.10 * p.softness;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            const double pr = (uu - 0.5) * vx + (vv - 0.5) * vy + 0.5;
            const double g1 = std::exp(-((pr - c) * (pr - c)) /
                                       (width * width));
            const double g2 = 0.5 * std::exp(-((pr - c2) * (pr - c2)) /
                                             (0.35 * 0.35));
            const double leak =
                std::clamp((g1 + g2) * inten, 0.0, 1.0);
            std::uint8_t m[4];
            MixPix(u + (static_cast<std::size_t>(y) * W + x) * 4,
                   o + (static_cast<std::size_t>(y) * W + x) * 4,
                   w_mix, m);
            // Warm orange leak, additive with a mild exposure lift.
            const double lift = leak * 255.0;
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            dp[0] = Clamp8(m[0] + lift * 0.15);
            dp[1] = Clamp8(m[1] + lift * 0.50);
            dp[2] = Clamp8(m[2] + lift * 1.00);
            dp[3] = m[3];
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.film_roll — vertical film-transport roll (FilmStrip.glsl
// simplified): A rolls out, a frame-line gap passes, B lands. Motion
// blur follows the roll speed; a small weave shakes the strip.
// dir 0 = roll up (default), 1 = roll down. int = blur amount.
// ------------------------------------------------------------------
void FilmRoll(const XerTransitionParams& p, double w,
              const std::uint8_t* u, const std::uint8_t* o,
              std::uint8_t* d, int W, int H) {
    const double gap = 0.06;                       // frame line frac
    const double x0 = w;
    const double slip =
        x0 * x0 * x0 * (x0 * (x0 * 6.0 - 15.0) + 10.0);   // smoother
    const double h = 1.0 - std::fabs(2.0 * w - 1.0);
    const double len = 0.30 * std::clamp(p.intensity, 0.0, 1.0) * h;
    const bool down = ((p.direction % 2) + 2) % 2 == 1;
    const double weave = 0.006 * std::sin(w * 47.0) * h * 0.5 *
                         std::clamp(p.intensity, 0.0, 1.0);
    const std::uint32_t seed = p.seed ^ 0xF11Cu;
    constexpr int kTaps = 10;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = std::clamp(
                (x + 0.5) / W + weave, 0.0, 1.0);
            const double vv = (y + 0.5) / H;
            const double jitter = Hash01(x, y, seed);
            double acc[4] = {0, 0, 0, 0};
            for (int i = 0; i < kTaps; ++i) {
                const double t = (i + jitter) / kTaps - 0.5;
                const double s = down ? vv - slip * (1.0 + gap) - t * len
                                      : vv + slip * (1.0 + gap) + t * len;
                // strip: [0,1)=A, [1,1+gap)=frame line,
                // [1+gap, 2+gap)=B shifted back into [0,1).
                std::uint8_t c[4];
                if (s < 0.0 || s >= 2.0 + gap) {
                    c[0] = 10; c[1] = 8; c[2] = 8; c[3] = 255;
                } else if (s < 1.0) {
                    SampleUV(u, W, H, uu, s, c);
                } else if (s < 1.0 + gap) {
                    c[0] = 10; c[1] = 8; c[2] = 8; c[3] = 255;
                } else {
                    SampleUV(o, W, H, uu, s - 1.0 - gap, c);
                }
                const double a = c[3] / 255.0;
                acc[0] += c[0] * a;
                acc[1] += c[1] * a;
                acc[2] += c[2] * a;
                acc[3] += a;
            }
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            const double denom = std::max(acc[3], 1e-5);
            dp[0] = Clamp8(acc[0] / denom);
            dp[1] = Clamp8(acc[1] / denom);
            dp[2] = Clamp8(acc[2] / denom);
            dp[3] = Clamp8(acc[3] / kTaps * 255.0);
        }
    }
}
// ------------------------------------------------------------------
// xer.tr.page_curl — corner curl approximation (ORIGINAL, not the HP
// BSD-3 InvertedPageCurl): a diagonal roll sweeps from the chosen
// corner; the band shows the outgoing frame compressed with cylinder
// shading, the revealed side carries a soft drop shadow.
// dir: 0=BR 1=BL 2=TR 3=TL. soft = roll width.
// ------------------------------------------------------------------
void PageCurl(const XerTransitionParams& p, double w,
              const std::uint8_t* u, const std::uint8_t* o,
              std::uint8_t* d, int W, int H) {
    // Diagonal coordinate from the picked corner: 0 at the corner,
    // 2 at the far corner.
    const int corner = ((p.direction % 4) + 4) % 4;
    const double rw = 0.18 + 0.25 * p.softness;   // roll half-width
    const double f = w * (2.0 + 2.0 * rw) - rw;   // front position
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            double uu = (x + 0.5) / W;
            double vv = (y + 0.5) / H;
            double td;
            switch (corner) {
                case 0: td = (1.0 - uu) + (1.0 - vv); break;  // BR
                case 1: td = uu + (1.0 - vv); break;          // BL
                case 2: td = (1.0 - uu) + vv; break;          // TR
                default: td = uu + vv; break;                 // TL
            }
            const double rel = td - f;
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            const std::uint8_t* up =
                u + (static_cast<std::size_t>(y) * W + x) * 4;
            const std::uint8_t* op =
                o + (static_cast<std::size_t>(y) * W + x) * 4;
            if (rel <= -rw) {
                // Fully turned: incoming, with a shadow gradient
                // fading in from the roll edge.
                const double sh =
                    1.0 - 0.35 * std::exp((rel + rw) * 3.0 / rw);
                for (int c = 0; c < 3; ++c) {
                    dp[c] = Clamp8(op[c] * sh);
                }
                dp[3] = op[3];
            } else if (rel < rw) {
                // The roll band: outgoing compressed ~3x toward the
                // line + sinusoidal cylinder shading + edge sheen.
                const double q = (rel + rw) / (2.0 * rw);   // 0..1
                const double t_src = f - rw + q * rw * 0.35;
                double su = uu, sv = vv;
                const double push = t_src - td;
                switch (corner) {
                    case 0: su -= push * 0.5; sv -= push * 0.5; break;
                    case 1: su += push * 0.5; sv -= push * 0.5; break;
                    case 2: su -= push * 0.5; sv += push * 0.5; break;
                    default: su += push * 0.5; sv += push * 0.5; break;
                }
                std::uint8_t c4[4];
                SampleUV(u, W, H, su, sv, c4);
                const double shade =
                    0.55 + 0.45 * std::cos(3.141592653589793 * (q - 0.5));
                const double sheen =
                    0.25 * std::exp(-std::fabs(q - 0.9) * 14.0);
                for (int c = 0; c < 3; ++c) {
                    dp[c] = Clamp8(c4[c] * shade + 255.0 * sheen);
                }
                dp[3] = c4[3];
            } else {
                dp[0] = up[0]; dp[1] = up[1];
                dp[2] = up[2]; dp[3] = up[3];
            }
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.directional_wipe — DirectionalWipe.glsl port: L1-normalized
// direction + smoothstep edge. dir = compass 0..7 (front travels
// that way), soft = edge feather.
// ------------------------------------------------------------------
void DirectionalWipe(const XerTransitionParams& p, double w,
                     const std::uint8_t* u, const std::uint8_t* o,
                     std::uint8_t* d, int W, int H) {
    double vx, vy;
    CompassVec(p.direction, &vx, &vy);
    const double nrm = std::fabs(vx) + std::fabs(vy);
    vx /= nrm;
    vy /= nrm;
    const double dd = vx * 0.5 + vy * 0.5;
    const double soft = p.softness;
    const double thresh = dd - 0.5 + w * (1.0 + soft);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            const double proj = vx * uu + vy * vv;
            // GL verbatim: m (B coverage) = 1 on the swept side
            // (proj < thresh), 0 ahead of the front; the front moves
            // toward +v, so `dir` is the direction the wipe travels.
            const double m = (w <= 0.0 ? 0.0 : 1.0) *
                             (1.0 - Smoothstep(-soft, 0.0, proj - thresh));
            RevealPix(
                u + (static_cast<std::size_t>(y) * W + x) * 4,
                o + (static_cast<std::size_t>(y) * W + x) * 4,
                m, d + (static_cast<std::size_t>(y) * W + x) * 4);
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.iris — CircleOpen.glsl port extended to three SDF shapes and
// open/close variants. dir: 0 circle-open, 1 diamond-open, 2 box-open,
// 3 circle-close, 4 diamond-close, 5 box-close. soft = edge feather.
// ------------------------------------------------------------------
void Iris(const XerTransitionParams& p, double w,
          const std::uint8_t* u, const std::uint8_t* o,
          std::uint8_t* d, int W, int H) {
    const int idx = ((p.direction % 6) + 6) % 6;
    const int shape = idx % 3;
    const bool closing = idx >= 3;
    const double soft = std::max(p.softness, 0.001);
    const double radius =
        (closing ? 1.0 - w : w) * (1.0 + soft);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double dx = (x + 0.5) / W - 0.5;
            const double dy = (y + 0.5) / H - 0.5;
            double sd;
            switch (shape) {
                case 0:   // circle
                    sd = std::sqrt(dx * dx + dy * dy) *
                         1.4142135623730951;
                    break;
                case 1:   // diamond
                    sd = std::fabs(dx) + std::fabs(dy);
                    break;
                default:  // box
                    sd = std::max(std::fabs(dx), std::fabs(dy)) * 2.0;
                    break;
            }
            const double inside =
                1.0 - Smoothstep(-soft, 0.0, sd - radius);
            // open: B inside the growing shape; close: A inside the
            // shrinking hole, B outside.
            const double coverB = closing ? 1.0 - inside : inside;
            RevealPix(
                u + (static_cast<std::size_t>(y) * W + x) * 4,
                o + (static_cast<std::size_t>(y) * W + x) * 4,
                coverB,
                d + (static_cast<std::size_t>(y) * W + x) * 4);
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.flash — exposure spike through white with overshoot
// (Premiere dip-to-white upgraded): both frames blow out past white
// at the cut so the handoff hides inside the overexposure.
// ------------------------------------------------------------------
void Flash(const XerTransitionParams& p, double w,
           const std::uint8_t* u, const std::uint8_t* o,
           std::uint8_t* d, int W, int H) {
    const double inten = std::clamp(p.intensity, 0.0, 1.0);
    const double env =
        std::pow(std::sin(3.141592653589793 * w), 0.6) * inten;
    const double gain = 1.0 + 2.4 * env;
    const double wash = 0.5 * env * env * 255.0;
    const int count = W * H;
    for (int i = 0; i < count; ++i) {
        std::uint8_t m[4];
        MixPix(u + static_cast<std::size_t>(i) * 4,
               o + static_cast<std::size_t>(i) * 4, w, m);
        std::uint8_t* dp = d + static_cast<std::size_t>(i) * 4;
        dp[0] = Clamp8(m[0] * gain + wash);
        dp[1] = Clamp8(m[1] * gain + wash);
        dp[2] = Clamp8(m[2] * gain + wash);
        dp[3] = m[3];
    }
}

// ------------------------------------------------------------------
// xer.tr.spin — WhipSpin.glsl port: camera roll with cubic speed,
// spin-direction motion blur (time-shutter taps), per-tap chroma
// split, zoom pulse at the cut. dir 0 = CCW, 1 = CW. int = blur.
// ------------------------------------------------------------------
void Spin(const XerTransitionParams& p, double w,
          const std::uint8_t* u, const std::uint8_t* o,
          std::uint8_t* d, int W, int H) {
    const double turns = (((p.direction % 2) + 2) % 2 == 1) ? -1.0 : 1.0;
    const double inten = std::clamp(p.intensity, 0.0, 1.0);
    const double h = 1.0 - std::fabs(2.0 * w - 1.0);
    const double shutter = 0.5 * inten * 0.1 * h;
    const double scale = 1.0 + 0.25 * h * h * h;
    const double chroma = 0.6 * inten;
    const double aspect = static_cast<double>(W) / std::max(H, 1);
    const double half_turn = 3.141592653589793 * turns;
    auto roll = [&](double t) {
        if (t < 0.5) {
            const double x = 2.0 * t;
            return half_turn * x * x * x;
        }
        const double x = 2.0 - 2.0 * t;
        return -half_turn * x * x * x;
    };
    constexpr int kTaps = 14;
    const std::uint32_t seed = p.seed ^ 0x5F1Eu;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W;
            const double vv = (y + 0.5) / H;
            const double qx = (uu - 0.5) * aspect;
            const double qy = vv - 0.5;
            const double jitter = Hash01(x, y, seed);
            double acc_r = 0, acc_g = 0, acc_b = 0;
            double w_r = 0, w_g = 0, w_b = 0;
            double alpha = 0;
            for (int i = 0; i < kTaps; ++i) {
                const double s = (i + jitter) / kTaps - 0.5;
                const double t = std::clamp(w + s * shutter, 0.0, 1.0);
                const double a = roll(t);
                const double cs = std::cos(a);
                const double sn = std::sin(a);
                double rx = (cs * qx + sn * qy) / scale;
                double ry = (-sn * qx + cs * qy) / scale;
                // mirror-wrap into [0,1]
                rx = rx / aspect + 0.5;
                ry = ry + 0.5;
                rx = std::fmod(rx + 2.0, 2.0);
                ry = std::fmod(ry + 2.0, 2.0);
                if (rx > 1.0) rx = 2.0 - rx;
                if (ry > 1.0) ry = 2.0 - ry;
                std::uint8_t c[4];
                SampleUV(t < 0.5 ? u : o, W, H, rx, ry, c);
                const double cw_r = std::max(1.0 - 4.0 * chroma * s, 0.0);
                const double cw_b = std::max(1.0 + 4.0 * chroma * s, 0.0);
                const double ca = c[3] / 255.0;
                acc_b += c[0] * ca * cw_b;  // BGRA: idx0=B (blue leads)
                acc_g += c[1] * ca;
                acc_r += c[2] * ca * cw_r;
                w_b += ca * cw_b;
                w_g += ca;
                w_r += ca * cw_r;
                alpha += ca;
            }
            std::uint8_t* dp =
                d + (static_cast<std::size_t>(y) * W + x) * 4;
            dp[0] = Clamp8(acc_b / std::max(w_b, 1e-5));
            dp[1] = Clamp8(acc_g / std::max(w_g, 1e-5));
            dp[2] = Clamp8(acc_r / std::max(w_r, 1e-5));
            dp[3] = Clamp8(alpha / kTaps * 255.0);
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.pixel_displace — RandomSquares.glsl-style per-block dissolve
// with the incoming image sliding in displaced per block (decays to
// rest as the block completes). seed varies the field; int scales
// displacement; soft = per-block edge width.
// ------------------------------------------------------------------
void PixelDisplace(const XerTransitionParams& p, double w,
                   const std::uint8_t* u, const std::uint8_t* o,
                   std::uint8_t* d, int W, int H) {
    const int block = std::max(8, std::min(W, H) / 16);
    const double soft = std::max(p.softness, 0.05);
    const double disp =
        0.5 * std::clamp(p.intensity, 0.0, 1.0);   // uv units max
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const int bx = x / block;
            const int by = y / block;
            const double r = Hash01(bx, by, p.seed);
            const double m =
                Smoothstep(0.0, -soft, r - (w * (1.0 + soft)));
            const double du =
                (Hash01(bx, by, p.seed ^ 0xA1u) - 0.5) * 2.0 * disp *
                (1.0 - m);
            const double dv =
                (Hash01(bx, by, p.seed ^ 0xB7u) - 0.5) * 2.0 * disp *
                (1.0 - m);
            std::uint8_t ob[4];
            SampleUV(o, W, H, (x + 0.5) / W + du,
                     (y + 0.5) / H + dv, ob);
            RevealPix(
                u + (static_cast<std::size_t>(y) * W + x) * 4,
                ob, m,
                d + (static_cast<std::size_t>(y) * W + x) * 4);
        }
    }
}

// ------------------------------------------------------------------
// xer.tr.ink — organic blob reveal (PerlinDissolve.glsl port): a
// seeded value-noise field; the threshold sweeps through it so B
// bleeds in along the noise isosurface. int scales blob frequency,
// soft = edge width, seed = field.
// ------------------------------------------------------------------
void Ink(const XerTransitionParams& p, double w,
         const std::uint8_t* u, const std::uint8_t* o,
         std::uint8_t* d, int W, int H) {
    const double soft = std::max(p.softness, 0.01);
    const double scale = 3.0 + 6.0 * std::clamp(p.intensity, 0.0, 1.0);
    const std::uint32_t seed = p.seed * 2654435761u + 12345u;
    const double pr = -soft + w * (1.0 + 2.0 * soft);
    const double aspect = static_cast<double>(W) / std::max(H, 1);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const double uu = (x + 0.5) / W * aspect;
            const double vv = (y + 0.5) / H;
            // two-octave value noise for more organic edges
            const double n =
                0.65 * ValueNoise(uu * scale, vv * scale, seed) +
                0.35 * ValueNoise(uu * scale * 2.3, vv * scale * 2.3,
                                  seed ^ 0x9E37u);
            const double coverB =
                1.0 - Smoothstep(pr - soft, pr + soft, n);
            RevealPix(
                u + (static_cast<std::size_t>(y) * W + x) * 4,
                o + (static_cast<std::size_t>(y) * W + x) * 4,
                coverB,
                d + (static_cast<std::size_t>(y) * W + x) * 4);
        }
    }
}

// ------------------------------------------------------------------
// Registry — id, label, picker group, params-in-use.
// ------------------------------------------------------------------
struct KernelEntry {
    const char* id;
    KernelFn fn;
};

constexpr KernelEntry kKernels[] = {
    {"xer.tr.whip_pan", &WhipPan},
    {"xer.tr.zoom", &Zoom},
    {"xer.tr.luma_fade", &LumaFade},
    {"xer.tr.morph", &Morph},
    {"xer.tr.glitch", &Glitch},
    {"xer.tr.chroma", &Chroma},
    {"xer.tr.light_leak", &LightLeak},
    {"xer.tr.film_roll", &FilmRoll},
    {"xer.tr.page_curl", &PageCurl},
    {"xer.tr.directional_wipe", &DirectionalWipe},
    {"xer.tr.iris", &Iris},
    {"xer.tr.flash", &Flash},
    {"xer.tr.spin", &Spin},
    {"xer.tr.pixel_displace", &PixelDisplace},
    {"xer.tr.ink", &Ink},
};

const KernelEntry* FindKernel(const std::string& id) {
    for (const KernelEntry& e : kKernels) {
        if (id == e.id) {
            return &e;
        }
    }
    return nullptr;
}

}  // namespace

const std::vector<XerTransitionInfo>& XerTransitions() {
    static const std::vector<XerTransitionInfo> kAll = {
        // Motion — camera-feel moves first (highest wow).
        {"xer.tr.whip_pan", "Whip Pan", "Motion", "dir,int"},
        {"xer.tr.zoom", "Zoom Blur", "Motion", "int"},
        {"xer.tr.spin", "Whip Spin", "Motion", "dir,int"},
        {"xer.tr.film_roll", "Film Roll", "Motion", "dir,int"},
        {"xer.tr.page_curl", "Page Curl", "Motion", "dir,soft"},
        // Stylize — signature looks.
        {"xer.tr.luma_fade", "Luma Fade", "Stylize", "dir,soft"},
        {"xer.tr.morph", "Morph", "Stylize", "int"},
        {"xer.tr.glitch", "Glitch", "Stylize", "int,seed"},
        {"xer.tr.chroma", "Chromatic Split", "Stylize", "int"},
        {"xer.tr.light_leak", "Light Leak", "Stylize", "dir,int,seed"},
        {"xer.tr.flash", "Flash", "Stylize", "int"},
        {"xer.tr.pixel_displace", "Pixel Displace", "Stylize",
         "soft,int,seed"},
        {"xer.tr.ink", "Ink Bleed", "Stylize", "soft,int,seed"},
        // Wipe — utility shapes.
        {"xer.tr.directional_wipe", "Directional Wipe", "Wipe",
         "dir,soft"},
        {"xer.tr.iris", "Iris", "Wipe", "dir,soft"},
    };
    return kAll;
}

bool XerTransitionKnown(const std::string& id) {
    return FindKernel(id) != nullptr;
}

XerTransitionParams ParseXerTransitionParams(const std::string& text) {
    XerTransitionParams p;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = text.find(';', pos);
        const std::string kv =
            text.substr(pos, end == std::string::npos
                              ? std::string::npos
                              : end - pos);
        const std::size_t eq = kv.find('=');
        if (eq != std::string::npos) {
            const std::string key = kv.substr(0, eq);
            const std::string val = kv.substr(eq + 1);
            char* tail = nullptr;
            const double num = std::strtod(val.c_str(), &tail);
            if (tail != val.c_str()) {
                if (key == "dir") {
                    p.direction = static_cast<int>(num);
                } else if (key == "soft") {
                    p.softness = num;
                } else if (key == "int") {
                    p.intensity = num;
                } else if (key == "seed") {
                    p.seed = static_cast<std::uint32_t>(num);
                }
            }
        }
        if (end == std::string::npos) {
            break;
        }
        pos = end + 1;
    }
    p.softness = std::clamp(p.softness, 0.0, 1.0);
    p.intensity = std::clamp(p.intensity, 0.0, 4.0);
    return p;
}

std::string FormatXerTransitionParams(const XerTransitionParams& p) {
    std::string out;
    auto emit = [&](const char* key, double v, double def) {
        if (v != def) {
            if (!out.empty()) out += ';';
            out += key;
            out += '=';
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", v);
            out += buf;
        }
    };
    emit("dir", p.direction, 0.0);
    emit("soft", p.softness, 0.15);
    emit("int", p.intensity, 1.0);
    emit("seed", p.seed, 0.0);
    return out;
}

bool ApplyXerTransition(const std::string& id,
                        const XerTransitionParams& params,
                        double weight,
                        const video::Frame& under,
                        const video::Frame& over,
                        video::Frame* out) {
    if (out == nullptr || under.Format() != video::PixelFormat::BGRA8 ||
        over.Format() != video::PixelFormat::BGRA8 ||
        !under.IsValid() || !over.IsValid()) {
        return false;
    }
    const int W = under.Width();
    const int H = under.Height();
    if (W <= 0 || H <= 0 || over.Width() != W || over.Height() != H) {
        return false;
    }
    if (!out->IsValid() || out->Width() != W || out->Height() != H ||
        out->Format() != video::PixelFormat::BGRA8) {
        out->Release();
        if (!out->Allocate(W, H, video::PixelFormat::BGRA8)) {
            return false;
        }
    }
    const KernelEntry* k = FindKernel(id);
    if (k == nullptr) {
        return false;
    }
    const double w = std::clamp(weight, 0.0, 1.0);
    // Exact endpoints, enforced dispatcher-side so no kernel can
    // drift at the edges of its envelope math.
    if (w <= 0.0) {
        std::memcpy(out->Data(), under.Data(), under.DataSize());
        return true;
    }
    if (w >= 1.0) {
        std::memcpy(out->Data(), over.Data(), over.DataSize());
        return true;
    }
    k->fn(params, w, under.Data(), over.Data(), out->Data(), W, H);
    return true;
}

}  // namespace XER::VideoEditor
