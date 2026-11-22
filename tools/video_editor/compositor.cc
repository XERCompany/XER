// compositor.cc — see compositor.h for the contract. All pixel work is
// packed-BGRA8 straight-alpha; per-pixel compositing math (src-over,
// named blend modes, crossfade premultiply) routes through
// XER::Bridge::Skia helpers and video::MixSystem::BlendFrames, while
// effect math lives in video::VideoEffects (which may use Skia filters
// internally). Custom kernels with no src/ equivalent (transitions,
// warps, masks, glass/bokeh) stay as local CPU loops.
#include "compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <limits>
#include <map>
#include <mutex>
#include <set>

#include "ai/object_track.h"
#include "ai/smart_reframe.h"
#include "app_runtime/app_runtime.h"
#include "color/ocio_transform.h"
#include "generated_source.h"
#include "js_fx.h"
#include "mix_system.h"
#include "plugin_host.h"
#include "video_effects.h"
#include "video_utils.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor {
namespace {

constexpr double kPi = 3.14159265358979323846;
// Adjacency tolerance for transition endpoint validation — same value
// as the timeline model's internal kEpsilon.
constexpr double kAdjacencyEpsilon = 1e-6;
// Sanity cap on forward decode steps per EnsureFrame call — protects
// against a decoder that never reports pts >= target (VFR pathologies,
// broken timestamps) from spinning forever. Real clips decode far
// fewer frames per call.
constexpr int kMaxDecodeSteps = 1000000;
// First-call seek lead-in: land the decoder ~0.25 s before the target
// so forward-discard stays short (Seek is keyframe-backward only).
constexpr double kSeekLeadSeconds = 0.25;

// ------------------------------------------------------------------
// Pixel helpers — packed BGRA8, STRAIGHT alpha (B,G,R,A byte order).
// ------------------------------------------------------------------

inline std::uint8_t Clamp8(double v) {
    return static_cast<std::uint8_t>(std::clamp(v + 0.5, 0.0, 255.0));
}

// BGRA8 <-> bridge-packed uint32 (R<<24|G<<16|B<<8|A) — same packing
// the Skia bridge uses internally, so BlendSrcOver/BlendModeApply round
// trips exactly through these.
inline std::uint32_t PackSkia(const std::uint8_t* bgra) {
    return XER::Bridge::Skia::MakeColorRGBA(bgra[2], bgra[1], bgra[0],
                                          bgra[3]);
}
inline void UnpackSkia(std::uint32_t rgba, std::uint8_t* bgra) {
    bgra[0] = static_cast<std::uint8_t>((rgba >> 8) & 0xFF);   // B
    bgra[1] = static_cast<std::uint8_t>((rgba >> 16) & 0xFF);  // G
    bgra[2] = static_cast<std::uint8_t>((rgba >> 24) & 0xFF);  // R
    bgra[3] = static_cast<std::uint8_t>(rgba & 0xFF);          // A
}

// Classic src-over for straight-alpha pixels: dst <- src OVER dst.
// Routes through Bridge::Skia::BlendSrcOver — the same straight-alpha
// formula the retired inline loop used, just shared instead of
// duplicated (the bridge impl is pure C++, so no Skia-runtime gate is
// needed). Fast paths kept: sa==0 -> dst unchanged; sa==255 -> src
// verbatim.
inline void AlphaOverPixel(std::uint8_t* dst, const std::uint8_t* src) {
    const int sa = src[3];
    if (sa == 0) {
        return;
    }
    if (sa == 255) {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
        return;
    }
    UnpackSkia(XER::Bridge::Skia::BlendSrcOver(PackSkia(dst),
                                             PackSkia(src)),
               dst);
}

void AlphaOverFrame(video::Frame* dst, const video::Frame& src) {
    const int count = dst->Width() * dst->Height();
    std::uint8_t* d = dst->Data();
    const std::uint8_t* s = src.Data();
    for (int i = 0; i < count; ++i) {
        AlphaOverPixel(d + i * 4, s + i * 4);
    }
}

void FillBackground(video::Frame* canvas,
                    const std::array<std::uint8_t, 4>& bg) {
    // bg_color is stored {r,g,b,a}; BGRA byte order is B,G,R,A.
    const std::uint8_t b = bg[2];
    const std::uint8_t g = bg[1];
    const std::uint8_t r = bg[0];
    const std::uint8_t a = bg[3];
    const int count = canvas->Width() * canvas->Height();
    std::uint8_t* p = canvas->Data();
    for (int i = 0; i < count; ++i) {
        p[i * 4 + 0] = b;
        p[i * 4 + 1] = g;
        p[i * 4 + 2] = r;
        p[i * 4 + 3] = a;
    }
}

void ClearFrame(video::Frame* frame) {
    if (frame->IsValid()) {
        std::memset(frame->Data(), 0, frame->DataSize());
    }
}

// ------------------------------------------------------------------
// Bilinear sampler — continuous coords where pixel (i,j) CENTER sits
// at (i+0.5, j+0.5). clamp_edges=true clamps taps into the frame
// (warp); false reports transparent-black outside [0,w]x[0,h] (slide).
// ------------------------------------------------------------------
bool SampleBilinearBGRA(const std::uint8_t* px, int w, int h,
                        double sx, double sy, bool clamp_edges,
                        std::uint8_t* out) {
    if (sx < 0.0 || sx > static_cast<double>(w) || sy < 0.0 ||
        sy > static_cast<double>(h)) {
        if (!clamp_edges) {
            out[0] = out[1] = out[2] = out[3] = 0;
            return false;
        }
        // clamp mode: fall through — the per-tap clamp below pins the
        // sample to the edge texel.
    }
    const double fx = sx - 0.5;
    const double fy = sy - 0.5;
    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const double tx = fx - static_cast<double>(x0);
    const double ty = fy - static_cast<double>(y0);
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;
    auto tap = [&](int x, int y) -> const std::uint8_t* {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return px + (static_cast<std::size_t>(y) * w + x) * 4;
    };
    const std::uint8_t* p00 = tap(x0, y0);
    const std::uint8_t* p10 = tap(x1, y0);
    const std::uint8_t* p01 = tap(x0, y1);
    const std::uint8_t* p11 = tap(x1, y1);
    const double w00 = (1.0 - tx) * (1.0 - ty);
    const double w10 = tx * (1.0 - ty);
    const double w01 = (1.0 - tx) * ty;
    const double w11 = tx * ty;
    for (int c = 0; c < 4; ++c) {
        out[c] = Clamp8(p00[c] * w00 + p10[c] * w10 + p01[c] * w01 +
                        p11[c] * w11);
    }
    return true;
}

// Bilinear whole-frame resample: dst center (x+0.5, y+0.5) reads src
// at ((x+0.5)*sw/dw - 0.5) in index space — the same center
// convention as SampleBilinearBGRA — with edges clamped. `dst` must
// already be allocated to the target size.
//
// Primary path is the shared video::utils::ResizeFrameWithSkia (same
// center convention via the bridge's ImageScale). When the Skia
// bridge is unavailable it returns false and the integer 8.8
// fixed-point fallback below runs instead — kept so no-Skia builds
// still get a real resample.
void ResampleBilinearBGRA(const video::Frame& src, video::Frame* dst) {
    const int sw = src.Width();
    const int sh = src.Height();
    const int dw = dst->Width();
    const int dh = dst->Height();
    const std::uint8_t* spx = src.Data();
    std::uint8_t* dpx = dst->Data();
    if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) {
        return;
    }
    // Same-size fast path — a plain copy.
    if (sw == dw && sh == dh) {
        std::memcpy(dpx, spx,
                    static_cast<std::size_t>(dw) * dh * 4);
        return;
    }
    if (video::utils::ResizeFrameWithSkia(src, *dst)) {
        return;
    }
    // Per-column source taps, shared by every row.
    std::vector<int> x0s(static_cast<std::size_t>(dw));
    std::vector<int> x1s(static_cast<std::size_t>(dw));
    std::vector<int> wxs(static_cast<std::size_t>(dw));
    const double kx = static_cast<double>(sw) / dw;
    for (int x = 0; x < dw; ++x) {
        const double fx = std::clamp((x + 0.5) * kx - 0.5, 0.0,
                                     static_cast<double>(sw - 1));
        const int x0 = static_cast<int>(fx);
        x0s[static_cast<std::size_t>(x)] = x0;
        x1s[static_cast<std::size_t>(x)] = std::min(x0 + 1, sw - 1);
        wxs[static_cast<std::size_t>(x)] = static_cast<int>(
            std::lround((fx - x0) * 256.0));
    }
    const double ky = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        const double fy = std::clamp((y + 0.5) * ky - 0.5, 0.0,
                                     static_cast<double>(sh - 1));
        const int y0 = static_cast<int>(fy);
        const int y1 = std::min(y0 + 1, sh - 1);
        const int wy = static_cast<int>(
            std::lround((fy - y0) * 256.0));
        const int wy_inv = 256 - wy;
        const std::uint8_t* r0 =
            spx + static_cast<std::size_t>(y0) * sw * 4;
        const std::uint8_t* r1 =
            spx + static_cast<std::size_t>(y1) * sw * 4;
        std::uint8_t* out = dpx + static_cast<std::size_t>(y) * dw * 4;
        for (int x = 0; x < dw; ++x) {
            const std::uint8_t* p00 =
                r0 + static_cast<std::size_t>(x0s[static_cast<std::size_t>(x)]) * 4;
            const std::uint8_t* p10 =
                r0 + static_cast<std::size_t>(x1s[static_cast<std::size_t>(x)]) * 4;
            const std::uint8_t* p01 =
                r1 + static_cast<std::size_t>(x0s[static_cast<std::size_t>(x)]) * 4;
            const std::uint8_t* p11 =
                r1 + static_cast<std::size_t>(x1s[static_cast<std::size_t>(x)]) * 4;
            const int wx = wxs[static_cast<std::size_t>(x)];
            const int w00 = (256 - wx) * wy_inv;
            const int w10 = wx * wy_inv;
            const int w01 = (256 - wx) * wy;
            const int w11 = wx * wy;
            for (int c = 0; c < 4; ++c) {
                out[x * 4 + c] = static_cast<std::uint8_t>(
                    (p00[c] * w00 + p10[c] * w10 + p01[c] * w01 +
                     p11[c] * w11 + 32768) >> 16);
            }
        }
    }
}

// ------------------------------------------------------------------
// Transition helpers
// ------------------------------------------------------------------

// Hard-edge directional test for the wipe family — pixel centers.
bool WipeReveals(Transition::Type type, int x, int y, int w, int h,
                 double weight) {
    switch (type) {
        case Transition::Type::WipeLeft:
            return (x + 0.5) / w < weight;
        case Transition::Type::WipeRight:
            return (x + 0.5) / w > 1.0 - weight;
        case Transition::Type::WipeUp:
            return (y + 0.5) / h < weight;
        case Transition::Type::WipeDown:
            return (y + 0.5) / h > 1.0 - weight;
        default:
            return false;
    }
}

bool IsWipe(Transition::Type type) {
    return type == Transition::Type::WipeLeft ||
           type == Transition::Type::WipeRight ||
           type == Transition::Type::WipeUp ||
           type == Transition::Type::WipeDown;
}

bool IsSlide(Transition::Type type) {
    return type == Transition::Type::SlideLeft ||
           type == Transition::Type::SlideRight ||
           type == Transition::Type::SlideUp ||
           type == Transition::Type::SlideDown;
}

// Per-layer pixel displacements for the slide family. The composite
// motion is toward <D>: under exits toward D by w*dim, over enters
// from the opposite side displaced (1-w)*dim. See compositor.h for
// the sampling convention.
void SlideOffsets(Transition::Type type, double w, int width, int height,
                  double* under_dx, double* under_dy, double* over_dx,
                  double* over_dy) {
    *under_dx = *under_dy = *over_dx = *over_dy = 0.0;
    switch (type) {
        case Transition::Type::SlideLeft:
            *under_dx = -w * width;
            *over_dx = (1.0 - w) * width;
            break;
        case Transition::Type::SlideRight:
            *under_dx = w * width;
            *over_dx = -(1.0 - w) * width;
            break;
        case Transition::Type::SlideUp:
            *under_dy = -w * height;
            *over_dy = (1.0 - w) * height;
            break;
        case Transition::Type::SlideDown:
            *under_dy = w * height;
            *over_dy = -(1.0 - w) * height;
            break;
        default:
            break;
    }
}

// Premultiplied-alpha lerp for one pixel (crossfade), expressed through
// the bridge's alpha helpers: premultiply both ends, lerp all four
// premultiplied channels, unpremultiply. Same math as the retired
// inline loop (premult rgb weighted by each side's alpha, then divide
// by the lerped alpha); channel rounding now goes through the bridge's
// (v*a+127)/255 int path, within 1 ulp of the old double math.
inline void CrossfadePixel(const std::uint8_t* u, const std::uint8_t* o,
                           double w, double iw, std::uint8_t* out) {
    const std::uint32_t up =
        XER::Bridge::Skia::PremultiplyAlpha(PackSkia(u));
    const std::uint32_t op =
        XER::Bridge::Skia::PremultiplyAlpha(PackSkia(o));
    const double ua = u[3] / 255.0;
    const double oa = o[3] / 255.0;
    const double pa = ua * iw + oa * w;
    if (pa <= 1e-9) {
        out[0] = out[1] = out[2] = out[3] = 0;
        return;
    }
    std::uint8_t ch[4];
    for (int c = 0; c < 4; ++c) {
        const int shift = 24 - c * 8;   // R,G,B,A packing order
        const double uc = (up >> shift) & 0xFF;
        const double oc = (op >> shift) & 0xFF;
        ch[c] = Clamp8(uc * iw + oc * w);
    }
    const std::uint32_t prem =
        (static_cast<std::uint32_t>(ch[0]) << 24) |
        (static_cast<std::uint32_t>(ch[1]) << 16) |
        (static_cast<std::uint32_t>(ch[2]) << 8) |
        static_cast<std::uint32_t>(ch[3]);
    UnpackSkia(XER::Bridge::Skia::UnpremultiplyAlpha(prem), out);
}

// ------------------------------------------------------------------
// Mask helpers — soft coverage in clip-frame pixel coordinates.
// ------------------------------------------------------------------

// Signed distance (pixels) to the mask shape's boundary at sample
// point (px, py): negative inside. Rect uses exact box distance (the
// larger of the two axis distances); ellipse approximates by
// normalized radius scaled by the smaller half-axis (exact for
// circles, close enough for a feathered edge). Non-finite or
// non-positive-size masks report +infinity (never inside).
double MaskSignedDistance(const ClipMask& mask, int w, int h,
                          double px, double py) {
    const double cx = static_cast<double>(mask.cx) * w;
    const double cy = static_cast<double>(mask.cy) * h;
    const double mw = static_cast<double>(mask.w) * w;
    const double mh = static_cast<double>(mask.h) * h;
    if (!std::isfinite(cx) || !std::isfinite(cy) ||
        !std::isfinite(mw) || !std::isfinite(mh) || !(mw > 0.0) ||
        !(mh > 0.0)) {
        return std::numeric_limits<double>::infinity();
    }
    const double dx = px - cx;
    const double dy = py - cy;
    if (mask.shape == MaskShape::Rect) {
        return std::max(std::fabs(dx) - mw * 0.5,
                        std::fabs(dy) - mh * 0.5);
    }
    const double rx = mw * 0.5;
    const double ry = mh * 0.5;
    const double qx = dx / rx;
    const double qy = dy / ry;
    return (std::sqrt(qx * qx + qy * qy) - 1.0) * std::min(rx, ry);
}

// Coverage [0,1] of one mask at pixel-center (px, py): a linear ramp
// of feather*min(w_px, h_px) pixels centered on the shape boundary
// (feather 0 -> hard edge at the boundary). inverted flips it. A
// degenerate mask covers nothing (or everything when inverted).
double MaskCoverage(const ClipMask& mask, int w, int h,
                    double px, double py) {
    const double mw = static_cast<double>(mask.w) * w;
    const double mh = static_cast<double>(mask.h) * h;
    double feather =
        std::max(0.0, static_cast<double>(mask.feather)) *
        std::min(mw, mh);
    if (!std::isfinite(feather)) {
        feather = 0.0;
    }
    const double d = MaskSignedDistance(mask, w, h, px, py);
    double coverage;
    if (!std::isfinite(d)) {
        coverage = 0.0;
    } else if (feather > 0.0) {
        coverage = std::clamp(0.5 - d / feather, 0.0, 1.0);
    } else {
        coverage = d < 0.0 ? 1.0 : 0.0;
    }
    return mask.inverted ? 1.0 - coverage : coverage;
}

// ------------------------------------------------------------------
// Blend helpers — every named BlendMode maps onto a Skia bridge blend
// name for video::MixSystem::BlendFrames. Normal keeps the dedicated
// alpha-over path; Add maps to "plus" (the premultiplied-add mode the
// old Add kernel approximated).
// ------------------------------------------------------------------

const char* BlendModeSkiaName(BlendMode mode) {
    switch (mode) {
        case BlendMode::Normal:      return "srcOver";
        case BlendMode::SrcOver:     return "srcOver";
        case BlendMode::Add:         return "plus";
        case BlendMode::Multiply:    return "multiply";
        case BlendMode::Screen:      return "screen";
        case BlendMode::Overlay:     return "overlay";
        case BlendMode::Darken:      return "darken";
        case BlendMode::Lighten:     return "lighten";
        case BlendMode::Clear:       return "clear";
        case BlendMode::Src:         return "src";
        case BlendMode::Dst:         return "dst";
        case BlendMode::DstOver:     return "dstOver";
        case BlendMode::SrcIn:       return "srcIn";
        case BlendMode::DstIn:       return "dstIn";
        case BlendMode::SrcOut:      return "srcOut";
        case BlendMode::DstOut:      return "dstOut";
        case BlendMode::SrcATop:     return "srcATop";
        case BlendMode::DstATop:     return "dstATop";
        case BlendMode::Xor:         return "xor";
        case BlendMode::Plus:        return "plus";
        case BlendMode::Modulate:    return "modulate";
        case BlendMode::ColorDodge:  return "colorDodge";
        case BlendMode::ColorBurn:   return "colorBurn";
        case BlendMode::HardLight:   return "hardLight";
        case BlendMode::SoftLight:   return "softLight";
        case BlendMode::Difference:  return "difference";
        case BlendMode::Exclusion:   return "exclusion";
        case BlendMode::Hue:         return "hue";
        case BlendMode::Saturation:  return "saturation";
        case BlendMode::Color:       return "color";
        case BlendMode::Luminosity:  return "luminosity";
    }
    return "srcOver";
}

// ------------------------------------------------------------------
// Glass / lens kernels. Spatial remaps need a pristine SOURCE copy —
// sampling the frame being written would smear — so every kernel
// snapshots `frame->Data()` first (one memcpy) and writes back.
// `scratch` is a reusable worker buffer owned by the caller.
//
// CPU path is bilinear-sampled and branch-light; a GPU backend can
// swap the inner loops 1:1 (each is a pure gather) — tracked as a
// follow-up against src/video/clip_gpu_accel.
// ------------------------------------------------------------------

// Thin-lens sphere map shared by crystal-ball and magnifier.
// For normalized radius r in [0,1], nz = sqrt(1-r^2) is the sphere
// height; refracting through it pulls the sample inward by
// (1-nz). bend>1 overshoots past the center and the image INVERTS —
// which is what a real ball lens does (scene appears flipped).
void ApplySphereRefraction(video::Frame* frame, double cx, double cy,
                           double radius, double bend, double chroma_px,
                           double edge_darken, double specular,
                           std::vector<std::uint8_t>* scratch) {
    const int w = frame->Width();
    const int h = frame->Height();
    scratch->assign(frame->Data(), frame->Data() +
                                  static_cast<std::size_t>(w) * h * 4);
    const std::uint8_t* src = scratch->data();
    std::uint8_t* dst = frame->Data();
    const double inv_r = radius > 0.0 ? 1.0 / radius : 0.0;
    // Fixed top-left key light for the specular dot.
    const double lx = cx - radius * 0.38;
    const double ly = cy - radius * 0.38;
    const double spec_r = radius * 0.16;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = dst + static_cast<std::size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = row + x * 4;
            const double dx = (x + 0.5) - cx;
            const double dy = (y + 0.5) - cy;
            const double r = std::sqrt(dx * dx + dy * dy) * inv_r;
            if (r >= 1.0) {
                continue;   // outside the glass — untouched
            }
            const double nz = std::sqrt(std::max(0.0, 1.0 - r * r));
            // Per-channel bend gives the chromatic split; order R>B
            // matches a positive (convex) element.
            std::uint8_t out[4];
            for (int c = 0; c < 4; ++c) {
                const double cb =
                    c == 3 ? 0.0 : chroma_px * (c == 2 ? -0.5 : 0.5);
                const double src_r =
                    r * (1.0 - (bend + cb * inv_r) * (1.0 - nz));
                double sx = cx + dx / r * src_r;   // r>0 here (r>=1
                double sy = cy + dy / r * src_r;   //  caught above)
                if (r < 1e-6) { sx = cx; sy = cy; }
                std::uint8_t t[4];
                SampleBilinearBGRA(src, w, h, sx, sy, true, t);
                out[c] = t[c];
            }
            // Fresnel-ish rim darkening + top-left specular spot sell
            // the glass surface.
            const double rim = std::clamp((r - 0.82) / 0.18, 0.0, 1.0);
            const double shade =
                1.0 - edge_darken * rim * rim * (3.0 - 2.0 * rim);
            double glint = 0.0;
            const double gx = (x + 0.5) - lx;
            const double gy = (y + 0.5) - ly;
            const double gd = std::sqrt(gx * gx + gy * gy) / spec_r;
            if (gd < 1.0) {
                glint = specular * 220.0 * (1.0 - gd) * (1.0 - gd);
            }
            for (int c = 0; c < 3; ++c) {
                p[c] = Clamp8(out[c] * shade + glint);
            }
            p[3] = out[3];
        }
    }
}

// Fluted / reeded glass — a row of cylindrical ribs. Each rib's local
// phase gives a surface normal that displaces the sample sideways;
// `softness` extra taps cheaply defocus it (real fluted glass is
// always a little hazy).
void ApplyRibbedGlassPixels(video::Frame* frame, double period_px,
                            double amplitude_px, bool horizontal,
                            double softness,
                            std::vector<std::uint8_t>* scratch) {
    const int w = frame->Width();
    const int h = frame->Height();
    scratch->assign(frame->Data(), frame->Data() +
                                  static_cast<std::size_t>(w) * h * 4);
    const std::uint8_t* src = scratch->data();
    std::uint8_t* dst = frame->Data();
    const double two_pi = 6.283185307179586;
    const int extra = static_cast<int>(std::round(softness * 4.0));
    const double spread = period_px * 0.12;
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = dst + static_cast<std::size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const double axis_pos = horizontal ? y + 0.5 : x + 0.5;
            const double phase = axis_pos / period_px;
            const double nx = -std::sin(phase * two_pi);  // rib normal
            const double off = nx * amplitude_px;
            double acc[4] = {};
            const int taps = 1 + extra * 2;
            for (int k = -extra; k <= extra; ++k) {
                const double jx = k * spread / std::max(1, extra);
                std::uint8_t t[4];
                if (horizontal) {
                    SampleBilinearBGRA(src, w, h, x + 0.5,
                                       y + 0.5 + off + jx, true, t);
                } else {
                    SampleBilinearBGRA(src, w, h, x + 0.5 + off + jx,
                                       y + 0.5, true, t);
                }
                for (int c = 0; c < 4; ++c) acc[c] += t[c];
            }
            std::uint8_t* p = row + x * 4;
            for (int c = 0; c < 4; ++c) {
                p[c] = Clamp8(acc[c] / taps);
            }
        }
    }
}

// Bokeh DOF — circle-of-confusion grows away from a focus region and
// each pixel resolves through a disk of K taps on a Vogel (golden
// angle) spiral, which reads as round/hex-ish aperture balls instead
// of a gaussian haze. Luma-weighted accumulation keeps speculars as
// distinct discs — the signature "real lens" look.
//
// `radial` chooses the focus geometry: linear band (tilt-shift style,
// band edges parallel to X) vs point + radius (vignette-style DOF).
void ApplyBokehDOFPixels(video::Frame* frame, bool radial, double cx,
                         double cy, double band, double falloff,
                         double max_radius, double highlight_boost,
                         std::vector<std::uint8_t>* scratch) {
    const int w = frame->Width();
    const int h = frame->Height();
    scratch->assign(frame->Data(), frame->Data() +
                                  static_cast<std::size_t>(w) * h * 4);
    const std::uint8_t* src = scratch->data();
    std::uint8_t* dst = frame->Data();
    constexpr int kTaps = 24;
    constexpr double kGolden = 2.399963229728653;
    // Vogel-spiral geometry is frame-invariant — hoist the sqrt and
    // cos/sin per tap out of the pixel loop (identical arithmetic,
    // so pixel-identical output; saves ~50 transcendentals/pixel).
    std::array<double, kTaps> tap_rad;
    std::array<double, kTaps> tap_cos;
    std::array<double, kTaps> tap_sin;
    for (int k = 0; k < kTaps; ++k) {
        tap_rad[k] = std::sqrt((k + 0.5) / kTaps);
        const double a = k * kGolden;
        tap_cos[k] = std::cos(a);
        tap_sin[k] = std::sin(a);
    }
    const double diag = std::sqrt(static_cast<double>(w) * w +
                                  static_cast<double>(h) * h);
    const double band_px = band * diag;
    const double fall_px = std::max(falloff * diag, 1.0);
    for (int y = 0; y < h; ++y) {
        std::uint8_t* row = dst + static_cast<std::size_t>(y) * w * 4;
        // Band mode: the CoC is a pure function of y — evaluated once
        // per row, and an in-focus row skips outright.
        double row_coc = -1.0;
        if (!radial) {
            const double dist = std::abs(y + 0.5 - cy);
            row_coc = std::clamp((dist - band_px) / fall_px, 0.0, 1.0) *
                      max_radius;
            if (row_coc < 0.75) {
                continue;   // in focus — untouched
            }
        }
        for (int x = 0; x < w; ++x) {
            double coc;
            if (radial) {
                const double dist =
                    std::sqrt((x + 0.5 - cx) * (x + 0.5 - cx) +
                              (y + 0.5 - cy) * (y + 0.5 - cy));
                coc = std::clamp((dist - band_px) / fall_px, 0.0, 1.0) *
                      max_radius;
            } else {
                coc = row_coc;
            }
            if (coc < 0.75) {
                continue;   // in focus — untouched
            }
            double acc[4] = {};
            double wsum = 0.0;
            for (int k = 0; k < kTaps; ++k) {
                const double rr = coc * tap_rad[k];
                std::uint8_t t[4];
                SampleBilinearBGRA(src, w, h, x + 0.5 + rr * tap_cos[k],
                                   y + 0.5 + rr * tap_sin[k], true, t);
                const double luma =
                    (0.2126 * t[2] + 0.7152 * t[1] + 0.0722 * t[0]) /
                    255.0;
                const double wt =
                    1.0 + highlight_boost * luma * luma;
                for (int c = 0; c < 4; ++c) acc[c] += t[c] * wt;
                wsum += wt;
            }
            std::uint8_t* p = row + x * 4;
            for (int c = 0; c < 4; ++c) {
                p[c] = Clamp8(acc[c] / wsum);
            }
        }
    }
}

// ------------------------------------------------------------------
// Decode session state
// ------------------------------------------------------------------

// One decoded frame slot. Three slots rotate prev/cur/incoming so the
// decode loop moves buffers instead of copying pixels — NextFrame
// resizes/fills `pixels` in place and each slot keeps its capacity.
struct FrameSlot {
    std::vector<std::uint8_t> pixels;  // packed BGRA8, w*h*4
    int width = 0;
    int height = 0;
    double pts = -1.0;   // media seconds; synthesized when decoder has none
};

struct ClipSession {
    const Clip* clip = nullptr;
    int track_index = -1;
    // Resolved decode source — clip->source_path after
    // CompositorOptions::media_resolver (empty = source verbatim,
    // resolved lazily on first open so a resolver never runs for
    // clips that contribute no frames).
    std::string decode_path;
    MediaDecoderPtr decoder;
    bool open_attempted = false;
    bool open_ok = false;
    bool no_video = false;   // opened fine but carries no pictures
    bool eof = false;        // NextFrame reported end-of-stream/error
    FrameSlot slots[3];
    int slot_prev = 0;
    int slot_cur = 1;
    int slot_in = 2;
    bool has_prev = false;
    bool has_cur = false;
    double synth_pts = 0.0;        // pts basis for timestamp-less streams
    double media_duration = 0.0;   // 0 = unknown (unreliable on mp4)
    double fps = 30.0;             // for synthesized pts spacing only
    int src_w = 0;
    int src_h = 0;
    double ext_start = 0.0;        // contribution window (incl. handles)
    double ext_end = 0.0;
    video::Frame frame;            // materialized selected source frame
    video::Frame layer;            // canvas-size warp output
    // Last-layer memo — when the full input tuple (state hash +
    // media/clip-local time + effective transform) repeats exactly,
    // `layer` still holds that render and the whole clip pipeline is
    // skipped. Only set for chains ChainCacheable() proves pure.
    std::uint64_t memo_hash = 0;
    bool memo_valid = false;
    // ChainCacheable() verdict for clip.effects (immutable within a
    // session) — tri-state so plugin-registry lookups happen once per
    // session, not once per frame. -1 = not computed yet.
    int chain_cacheable = -1;
};

enum class EnsureResult { kFrame, kNoContent, kError };

// ------------------------------------------------------------------
// Effect dispatch — the curated ClipEffect.type table. Params are
// clamped and missing params fall back to the documented defaults.
// `skia_only` entries are skipped (with a once-per-type warning) when
// the Skia bridge is unavailable.
// ------------------------------------------------------------------
const std::set<std::string>& KnownEffectTypes() {
    static const std::set<std::string> types = {
        "brightness_contrast", "grayscale",   "box_blur",
        "gaussian_blur",      "flip_h",      "flip_v",
        "sharpen",            "edge_detect", "emboss",
        "vignette",           "chromatic_aberration",
        "pixelate",           "oil_paint",   "motion_blur",
        "chroma_key",         "color_balance", "posterize",
        "lens_sphere",        "magnifier",     "ribbed_glass",
        "bokeh_dof",          "bokeh_blur",
    };
    return types;
}

bool EffectRequiresSkia(const std::string& type) {
    static const std::set<std::string> skia_only = {
        "sharpen", "edge_detect", "emboss",   "vignette",
        "chromatic_aberration", "pixelate", "oil_paint", "motion_blur",
    };
    return skia_only.count(type) != 0;
}

// "script:<id>" effect types resolve through the JS fx registry
// (assets/scripts/fx/fx.json) — loaded once per process. Preview and
// export compositors can each lazily trigger the load from their own
// worker thread, so the one-shot is a call_once, not a raced flag.
constexpr const char* kScriptFxPrefix = "script:";
std::once_flag g_jsfx_catalog_once;
std::string g_jsfx_catalog_error;   // written inside call_once only

float EffectParam(const ClipEffect& effect, std::size_t index,
                  float fallback) {
    return index < effect.params.size() ? effect.params[index] : fallback;
}

int EffectParamInt(const ClipEffect& effect, std::size_t index,
                   int fallback) {
    return static_cast<int>(std::lround(
        static_cast<double>(EffectParam(effect, index,
                                        static_cast<float>(fallback)))));
}

double EffectParamDouble(const ClipEffect& effect, std::size_t index,
                         double fallback) {
    return static_cast<double>(
        EffectParam(effect, index, static_cast<float>(fallback)));
}

// ------------------------------------------------------------------
// Effect-chain cache key material (see effect_frame_cache.h). The
// state hash folds every clip-space input to the post-mask frame:
// source identity, the ORDERED (effect type, raw param bits) list,
// mask field bits, canvas dims (generated matte/bars adopt them) and
// the preview-quality tier (ApplyReduced changes pixels).
// ------------------------------------------------------------------
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t HashRaw(std::uint64_t h, const void* data, std::size_t n) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= static_cast<std::uint64_t>(p[i]);
        h *= kFnvPrime;
    }
    return h;
}
std::uint64_t HashStr(std::uint64_t h, const std::string& s) {
    return HashRaw(h, s.data(), s.size());
}
std::uint64_t HashU64(std::uint64_t h, std::uint64_t v) {
    return HashRaw(h, &v, sizeof(v));
}
// Floats/doubles are hashed on their BIT pattern — any param change,
// including sign-of-zero or NaN payloads, yields a new key (extra-safe:
// a bit-identical param produces identical pixels by determinism).
std::uint64_t HashF32(std::uint64_t h, float v) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return HashU64(h, bits);
}
std::uint64_t HashF64(std::uint64_t h, double v) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    return HashU64(h, bits);
}

// True when every effect in the chain is a pure function of
// (pixels, params): named kernels, host-compiled builtins (incl.
// "plugin:<id>" records that resolve to a builtin — their DLL kernels
// were ported in-house), and unknown types (skipped at apply time,
// pixels untouched). Excluded: script: (receives clip-local time and
// may keep ctx state), ocio: (profiles can rescan mid-session) and
// plugin: resolving to a real DLL (third-party code may be stateful).
bool ChainCacheable(const std::vector<ClipEffect>& effects) {
    for (const ClipEffect& e : effects) {
        const std::string& ty = e.type;
        if (ty.rfind(kScriptFxPrefix, 0) == 0 ||
            ty.rfind("ocio:", 0) == 0) {
            return false;
        }
        if (ty.rfind("plugin:", 0) == 0) {
            const PluginDesc* desc = PluginHost::ForEffectType(ty);
            if (desc == nullptr || !desc->builtin) {
                return false;
            }
        }
    }
    return true;
}

// `decode_path` is the resolver output (empty when no resolver ran)
// — hashed alongside source_path so a proxy toggle or a fresh proxy
// landing invalidates memoized/cached layers instead of serving
// pixels decoded at the other resolution.
std::uint64_t ClipStateHash(const Clip& clip, double preview_quality,
                            int canvas_w, int canvas_h,
                            const std::string& decode_path) {
    std::uint64_t h = kFnvOffset;
    h = HashStr(h, clip.source_path);
    h = HashStr(h, decode_path);
    h = HashU64(h, static_cast<std::uint64_t>(clip.source.kind));
    h = HashStr(h, clip.source.param);
    h = HashU64(h, static_cast<std::uint64_t>(clip.effects.size()));
    for (const ClipEffect& e : clip.effects) {
        h = HashStr(h, e.type);
        h = HashU64(h, static_cast<std::uint64_t>(e.params.size()));
        if (!e.params.empty()) {
            h = HashRaw(h, e.params.data(),
                        e.params.size() * sizeof(float));
        }
    }
    // Smart-reframe track — the warp reads it, so a re-analysis must
    // bust both the frame cache and the layer memo.
    h = HashU64(h, static_cast<std::uint64_t>(clip.reframe.samples.size()));
    for (const ReframeSample& s : clip.reframe.samples) {
        h = HashF64(h, s.t);
        h = HashF32(h, s.cx);
        h = HashF32(h, s.cy);
        h = HashF32(h, s.w);
        h = HashF32(h, s.h);
        h = HashF32(h, s.confidence);
        h = HashU64(h, s.scene_boundary ? 1ull : 0ull);
    }
    // Object track — the xer.fx.track_mosaic region resolves from it
    // per frame, so a re-track must bust caches exactly like reframe.
    h = HashStr(h, clip.object_track.label);
    h = HashU64(h, static_cast<std::uint64_t>(
                      clip.object_track.samples.size()));
    for (const ReframeSample& s : clip.object_track.samples) {
        h = HashF64(h, s.t);
        h = HashF32(h, s.cx);
        h = HashF32(h, s.cy);
        h = HashF32(h, s.w);
        h = HashF32(h, s.h);
        h = HashF32(h, s.confidence);
        h = HashU64(h, s.scene_boundary ? 1ull : 0ull);
    }
    h = HashU64(h, static_cast<std::uint64_t>(clip.masks.size()));
    for (const ClipMask& m : clip.masks) {
        h = HashU64(h, static_cast<std::uint64_t>(m.shape));
        h = HashF32(h, m.cx);
        h = HashF32(h, m.cy);
        h = HashF32(h, m.w);
        h = HashF32(h, m.h);
        h = HashF32(h, m.feather);
        h = HashU64(h, m.inverted ? 1ull : 0ull);
    }
    h = HashF64(h, preview_quality);
    h = HashU64(h, (static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(canvas_w))
                    << 32) |
                       static_cast<std::uint32_t>(canvas_h));
    return h;
}

// Microsecond quantization for cache time fields — absorbs sub-µs FP
// noise in t/src_t while staying far below any frame interval.
std::int64_t QuantizeToUs(double v) {
    return static_cast<std::int64_t>(std::llround(v * 1e6));
}

// Last-layer memo key: everything the warp-output layer depends on —
// the clip-space frame identity (state_hash + time) plus the effective
// transform. A repeat of the exact tuple skips decode, chain AND warp.
std::uint64_t MemoHash(std::uint64_t state_hash, std::int64_t time_us,
                       double u, const Transform& tr) {
    std::uint64_t h = state_hash;
    h = HashU64(h, static_cast<std::uint64_t>(time_us));
    h = HashF64(h, u);
    h = HashF32(h, tr.offset_x);
    h = HashF32(h, tr.offset_y);
    h = HashF32(h, tr.scale);
    h = HashF32(h, tr.rotation_deg);
    h = HashF32(h, tr.opacity);
    return h;
}

}  // namespace

// ------------------------------------------------------------------
// Compositor::Impl — all state lives here so the header stays slim.
// ------------------------------------------------------------------
struct Compositor::Impl {
    Project project_;
    int canvas_w = 0;
    int canvas_h = 0;
    double duration = 0.0;
    MediaDecoderFactory decoder_factory;
    // CompositorOptions::media_resolver — clip.source_path -> decode
    // path (proxy seam). Empty means "open the source verbatim" —
    // the export configuration, always.
    std::function<std::string(const std::string&)> media_resolver;
    bool skia_available = false;
    bool initialized = false;
    std::atomic<bool> cancelled{false};
    std::vector<std::string> warnings;
    std::set<std::string> warned_once;
    video::Frame canvas;
    video::Frame transition_scratch;   // canvas-size transition output
    // JS fx runtime owned by THIS compositor's worker thread — created
    // lazily on the first scripted effect/transition. V8 isolates are
    // thread-confined, so each Compositor (preview engine, export
    // engine) gets its own. nullptr return = scripts unavailable or
    // failed to load (already Warn'ed once).
    std::unique_ptr<JsFxRuntime> js_fx;
    bool js_fx_tried = false;
    // SetPreviewQuality() — <1.0 routes the heavy spatial kernels
    // through a downscale->effect->upscale detour (preview only).
    double preview_quality_ = 1.0;
    video::Frame fx_lo_;   // reduced-res work frame (grows once)
    // Injected effect-chain result cache — borrowed, never owned;
    // nullptr disables caching (export/screenshot compositors). The
    // owner (PreviewEngine, MonitorWidget) keeps it alive across the
    // session reopens that rebuild this compositor.
    std::shared_ptr<EffectFrameCache> frame_cache;
    JsFxRuntime* JsFx() {
        if (!js_fx_tried) {
            js_fx_tried = true;
            std::call_once(g_jsfx_catalog_once, [] {
                const std::filesystem::path dir =
                    AppRuntime::SharedAssetsConfig().assets_dir /
                    "scripts" / "fx";
                JsFxRuntime::LoadCatalog(dir.string(),
                                         &g_jsfx_catalog_error);
            });
            js_fx = std::make_unique<JsFxRuntime>();
            std::string fx_error;
            if (!js_fx->EnsureLoaded(&fx_error)) {
                Warn("js fx unavailable: " + fx_error);
                js_fx.reset();
            }
        }
        return js_fx.get();
    }
    std::map<std::string, ClipSession> sessions;
    std::vector<int> video_track_indices;
    std::vector<const Transition*> valid_transitions;
    // Reused per-frame scratch — avoids a heap alloc per track per
    // RenderFrameAt call (export renders thousands of frames).
    std::vector<ClipSession*> candidates_scratch;
    bool warned_ambiguous = false;

    bool Fail(std::string* error_out, const std::string& message) {
        if (error_out != nullptr) {
            *error_out = message;
        }
        return false;
    }

    // Dedup'd warning sink — Init scans + runtime notes share it.
    void Warn(const std::string& message) {
        if (warned_once.insert(message).second) {
            warnings.push_back(message);
        }
    }

    bool Init(const Project& project, const CompositorOptions& options,
              std::string* error_out,
              std::vector<std::string>* warnings_out) {
        Shutdown();
        if (project.width <= 0 || project.height <= 0) {
            return Fail(error_out, "invalid canvas size");
        }
        if (project.fps_num <= 0 || project.fps_den <= 0) {
            return Fail(error_out, "invalid project fps");
        }
        project_ = project;   // snapshot — later edits never affect us
        canvas_w = project.width;
        canvas_h = project.height;
        duration = project.duration();
        decoder_factory = options.decoder_factory;
        if (!decoder_factory) {
            decoder_factory = DefaultDecoderFactory();
        }
        frame_cache = options.frame_cache;
        media_resolver = options.media_resolver;
        skia_available = XER::Bridge::Skia::IsAvailable();
        cancelled.store(false, std::memory_order_relaxed);

        // Pre-scan clip effects: unknown types and Skia-only effects
        // (when Skia is missing) warn once here; apply-time skips
        // re-warn through the same dedup set.
        for (const auto& clip : project_.clips) {
            for (const auto& effect : clip.effects) {
                if (effect.type.rfind("plugin:", 0) == 0 ||
                    effect.type.rfind("builtin:", 0) == 0) {
                    if (PluginHost::ForEffectType(effect.type) ==
                        nullptr) {
                        Warn("plugin effect '" + effect.type +
                             "' not loaded");
                    }
                } else if (effect.type.rfind("ocio:", 0) == 0) {
                    if (Color::FindOcioProfile(effect.type.substr(5)) ==
                        nullptr) {
                        Warn("ocio profile '" + effect.type.substr(5) +
                             "' not found");
                    }
                } else if (effect.type.rfind(kScriptFxPrefix, 0) == 0) {
                    const std::string fx_id =
                        effect.type.substr(std::strlen(kScriptFxPrefix));
                    bool found = false;
                    for (const auto& d : JsFxRuntime::VideoEffects()) {
                        if (d.id == fx_id) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        Warn("script effect '" + fx_id +
                             "' not registered");
                    }
                } else if (KnownEffectTypes().count(effect.type) == 0) {
                    Warn("unknown clip effect type '" + effect.type + "'");
                } else if (EffectRequiresSkia(effect.type) &&
                           !skia_available) {
                    Warn("effect '" + effect.type +
                         "' requires Skia; skipped");
                }
            }
        }

        // Validate stored transitions up front: both endpoints must
        // exist, share a VIDEO track, and be adjacent. Invalid entries
        // (hand-built or otherwise unchecked projects) warn + are
        // ignored rather than failing Init.
        for (const auto& transition : project_.transitions) {
            const Clip* from = FindClip(project_, transition.from_clip);
            const Clip* to = FindClip(project_, transition.to_clip);
            bool ok = from != nullptr && to != nullptr &&
                      from->track == to->track && from->track >= 0 &&
                      from->track <
                          static_cast<int>(project_.tracks.size()) &&
                      project_.tracks[static_cast<std::size_t>(from->track)]
                              .kind == TrackKind::Video &&
                      std::fabs(to->timeline_start -
                                ClipEndSeconds(*from)) <=
                          kAdjacencyEpsilon;
            if (ok) {
                valid_transitions.push_back(&transition);
            } else {
                Warn("transition '" + transition.from_clip + "' -> '" +
                     transition.to_clip +
                     "' is not between adjacent clips on a video track; "
                     "ignored");
            }
        }

        // One session per clip on a VIDEO track (audio-track clips are
        // the mixer's concern). Contribution windows extend past the
        // nominal range by the transition handles.
        for (const auto& clip : project_.clips) {
            const Track* track = FindTrack(project_, clip.track);
            if (track == nullptr || track->kind != TrackKind::Video) {
                continue;
            }
            ClipSession session;
            session.clip = &clip;
            session.track_index = clip.track;
            // Resolve the decode path once per session — generated
            // clips own no media file and never reach the decoder.
            if (media_resolver && !IsGeneratedSource(clip.source) &&
                !clip.source_path.empty()) {
                session.decode_path =
                    media_resolver(clip.source_path);
            }
            const auto handles = ClipTransitionHandles(project_, clip.id);
            session.ext_start = clip.timeline_start - handles.first;
            session.ext_end = ClipEndSeconds(clip) + handles.second;
            sessions.emplace(clip.id, std::move(session));
        }
        for (int i = 0; i < static_cast<int>(project_.tracks.size());
             ++i) {
            if (project_.tracks[static_cast<std::size_t>(i)].kind ==
                TrackKind::Video) {
                video_track_indices.push_back(i);
            }
        }
        initialized = true;
        if (warnings_out != nullptr) {
            warnings_out->insert(warnings_out->end(), warnings.begin(),
                                 warnings.end());
        }
        return true;
    }

    void Shutdown() {
        for (auto& entry : sessions) {
            ResetSession(entry.second);
        }
        sessions.clear();
        canvas.Release();
        transition_scratch.Release();
        video_track_indices.clear();
        valid_transitions.clear();
        warnings.clear();
        warned_once.clear();
        warned_ambiguous = false;
        project_ = Project{};
        decoder_factory = nullptr;
        media_resolver = nullptr;
        frame_cache.reset();
        skia_available = false;
        initialized = false;
        duration = 0.0;
        canvas_w = 0;
        canvas_h = 0;
        preview_quality_ = 1.0;
        fx_lo_.Release();
        cancelled.store(false, std::memory_order_relaxed);
    }

    // Full decode-state reset — used both by Shutdown and by the
    // per-frame session close (t >= ext_end). Frames are freed (not
    // just cleared) so long exports don't accumulate per-clip buffers.
    void ResetSession(ClipSession& s) {
        s.decoder.reset();
        for (auto& slot : s.slots) {
            std::vector<std::uint8_t>().swap(slot.pixels);
            slot.width = 0;
            slot.height = 0;
            slot.pts = -1.0;
        }
        s.slot_prev = 0;
        s.slot_cur = 1;
        s.slot_in = 2;
        s.has_prev = false;
        s.has_cur = false;
        s.open_attempted = false;
        s.open_ok = false;
        s.no_video = false;
        s.eof = false;
        s.synth_pts = 0.0;
        s.media_duration = 0.0;
        s.fps = 30.0;
        s.src_w = 0;
        s.src_h = 0;
        s.frame.Release();
        s.layer.Release();
        s.memo_hash = 0;
        s.memo_valid = false;
        s.chain_cacheable = -1;
    }

    void CloseFinishedSessions(double t) {
        for (auto& entry : sessions) {
            ClipSession& s = entry.second;
            if (s.open_attempted && t >= s.ext_end) {
                ResetSession(s);
            }
        }
    }

    static bool InWindow(const ClipSession& s, double t) {
        return s.ext_start <= t && t < s.ext_end;
    }

    // ---- decode ----------------------------------------------------

    // Decodes the next frame into the incoming slot and rotates
    // prev<-cur<-in (zero pixel copies — buffers keep capacity).
    bool DecodeNext(ClipSession& s) {
        FrameSlot& in = s.slots[static_cast<std::size_t>(s.slot_in)];
        int w = 0;
        int h = 0;
        if (!s.decoder->NextFrame(&in.pixels, &w, &h)) {
            return false;
        }
        const std::size_t expected =
            static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
            4u;
        if (w <= 0 || h <= 0 || in.pixels.size() != expected) {
            return false;   // malformed frame — treat as stream end
        }
        double pos = s.decoder->GetPositionSeconds();
        if (pos < 0.0) {
            // No timestamps: synthesize monotonic pts (previous +
            // 1/fps) so ordering still works. synth_pts is seeded from
            // the post-Seek preset position at open.
            pos = s.synth_pts + 1.0 / s.fps;
        }
        s.synth_pts = pos;
        in.width = w;
        in.height = h;
        in.pts = pos;
        const int next_prev = s.slot_cur;
        const int next_cur = s.slot_in;
        const int next_in = s.slot_prev;
        s.slot_prev = next_prev;
        s.slot_cur = next_cur;
        s.slot_in = next_in;
        s.has_prev = s.has_cur;
        s.has_cur = true;
        return true;
    }

    // Sequential forward decode + nearest-PTS selection. Returns the
    // chosen frame slot via `out_slot`; kNoContent means the clip has
    // no pictures (contributes nothing, NOT fatal); kError sets
    // error_out (fatal for export).
    EnsureResult EnsureFrame(ClipSession& s, double target,
                             const FrameSlot** out_slot,
                             std::string* error_out) {
        if (s.no_video) {
            return EnsureResult::kNoContent;
        }
        if (!s.open_ok) {
            // Lazy open on first contributing frame. EnableAudioTap is
            // deliberately NOT called — the audio mixer owns its own
            // decoder sessions.
            s.open_attempted = true;
            s.decoder = decoder_factory();
            if (!s.decoder) {
                Fail(error_out,
                     "decoder factory returned no decoder");
                return EnsureResult::kError;
            }
            std::string open_error;
            // decode_path was resolved at session Init (proxy seam);
            // empty means "the source path verbatim" — always the
            // export configuration.
            const std::string& open_path =
                s.decode_path.empty() ? s.clip->source_path
                                      : s.decode_path;
            if (!s.decoder->Open(open_path, &open_error)) {
                Fail(error_out,
                     "cannot open '" + s.clip->source_path + "'" +
                         (open_path != s.clip->source_path
                              ? " (via '" + open_path + "')"
                              : "") +
                         (open_error.empty() ? "" : ": " + open_error));
                return EnsureResult::kError;
            }
            // GetDurationSeconds/GetFps are best-effort — encoder
            // mp4s report 0.0/unusual rates, so duration>0 gates the
            // src_t clamp and fps only spaces synthesized pts.
            s.media_duration = s.decoder->GetDurationSeconds();
            const double reported_fps = s.decoder->GetFps();
            s.fps = reported_fps > 0.0 ? reported_fps : 30.0;
            s.src_w = s.decoder->GetWidth();
            s.src_h = s.decoder->GetHeight();
            if (s.src_w <= 0 || s.src_h <= 0) {
                s.no_video = true;
                Warn("clip '" + s.clip->id +
                     "' has no video stream — skipped");
                return EnsureResult::kNoContent;
            }
            // Best-effort seek near the target; failure is fine —
            // forward decode still reaches it.
            s.decoder->Seek(std::max(0.0, target - kSeekLeadSeconds));
            s.synth_pts =
                std::max(0.0, s.decoder->GetPositionSeconds());
            s.open_ok = true;
        }

        // Serve(target): decode forward until cur.pts >= target (the
        // bracket) or EOF; then pick the NEARER of {prev, cur} by
        // |pts - target|, ties -> prev (earlier). EOF before target
        // freezes on the last decoded frame.
        int steps = 0;
        for (;;) {
            if (cancelled.load(std::memory_order_relaxed)) {
                Fail(error_out, "export cancelled");
                return EnsureResult::kError;
            }
            if (s.has_cur &&
                s.slots[static_cast<std::size_t>(s.slot_cur)].pts >=
                    target) {
                break;
            }
            if (s.eof) {
                break;
            }
            if (!DecodeNext(s)) {
                s.eof = true;
                continue;
            }
            if (++steps > kMaxDecodeSteps) {
                Fail(error_out,
                     "clip '" + s.clip->id +
                         "': decoder did not reach frame target");
                return EnsureResult::kError;
            }
        }
        if (!s.has_cur) {
            Fail(error_out,
                 "clip '" + s.clip->id + "' produced no decodable frames");
            return EnsureResult::kError;
        }
        const FrameSlot& cur = s.slots[static_cast<std::size_t>(s.slot_cur)];
        if (s.has_prev) {
            const FrameSlot& prev =
                s.slots[static_cast<std::size_t>(s.slot_prev)];
            const double d_cur = std::fabs(cur.pts - target);
            const double d_prev = std::fabs(prev.pts - target);
            *out_slot = (d_prev <= d_cur) ? &prev : &cur;
        } else {
            *out_slot = &cur;
        }
        return EnsureResult::kFrame;
    }

    // ---- effects ---------------------------------------------------

    // Preview-quality detour for the heavy spatial kernels
    // (SetPreviewQuality < 1): the clip frame is bilinear-downsampled
    // into fx_lo_, `apply` runs the kernel on the reduced frame (the
    // lambda must scale its pixel-space params by lo/full size), then
    // the result is bilinear-upsampled back in place. Effect-free
    // regions of the frame still soften a little from the resample —
    // acceptable at monitor sizes, and export never takes this path.
    template <typename Fn>
    void ApplyReduced(video::Frame* frame, Fn&& apply) {
        const int lw = std::max(
            1, static_cast<int>(std::lround(
                   frame->Width() * preview_quality_)));
        const int lh = std::max(
            1, static_cast<int>(std::lround(
                   frame->Height() * preview_quality_)));
        if (!fx_lo_.IsValid() || fx_lo_.Width() != lw ||
            fx_lo_.Height() != lh ||
            fx_lo_.Format() != video::PixelFormat::BGRA8) {
            fx_lo_.Release();
            if (!fx_lo_.Allocate(lw, lh, video::PixelFormat::BGRA8)) {
                return;   // alloc fail — leave the frame untouched
            }
        }
        ResampleBilinearBGRA(*frame, &fx_lo_);
        apply(&fx_lo_);
        ResampleBilinearBGRA(fx_lo_, frame);
    }

    // xer.fx.track_mosaic: pixelate the clip's tracked object box in
    // CLIP-FRAME space (the track stores frame fractions, so the box
    // maps 1:1 onto the decoded frame and rides the later warp with
    // the content — the censor follows whatever the transform does).
    // `clip_time_s` is the clip-local u; source time resolves as
    // in_point + u*rate, matching the decode-branch math. An empty
    // track (or a degenerate box) leaves the frame untouched.
    void ApplyTrackedMosaic(ClipSession& s, const ClipEffect& effect,
                          double clip_time_s) {
        video::Frame& frame = s.frame;
        const Clip& clip = *s.clip;
        if (!frame.IsValid() ||
            frame.Format() != video::PixelFormat::BGRA8 ||
            clip.object_track.samples.empty()) {
            return;
        }
        const double rate =
            std::isfinite(clip.playback_rate) && clip.playback_rate > 0.0
                ? clip.playback_rate
                : 1.0;
        const double src_t = clip.in_point + clip_time_s * rate;
        const double smooth = std::clamp(
            EffectParamDouble(effect, 2, 0.5), 0.0, 1.0);
        const Ai::TrackBox box =
            Ai::TrackBoxAt(clip.object_track, src_t, smooth);
        if (box.w <= 0.0 || box.h <= 0.0) {
            return;
        }
        const int block = std::clamp(EffectParamInt(effect, 0, 12),
                                     2, 128);
        const double pad =
            std::clamp(EffectParamDouble(effect, 1, 0.15), 0.0, 1.0);
        const double fw = static_cast<double>(frame.Width());
        const double fh = static_cast<double>(frame.Height());
        const double pw = box.w * fw * (1.0 + pad);
        const double ph = box.h * fh * (1.0 + pad);
        const int x0 = std::clamp(
            static_cast<int>(std::floor(box.cx * fw - pw * 0.5)), 0,
            frame.Width());
        const int y0 = std::clamp(
            static_cast<int>(std::floor(box.cy * fh - ph * 0.5)), 0,
            frame.Height());
        const int x1 = std::clamp(
            static_cast<int>(std::ceil(box.cx * fw + pw * 0.5)), 0,
            frame.Width());
        const int y1 = std::clamp(
            static_cast<int>(std::ceil(box.cy * fh + ph * 0.5)), 0,
            frame.Height());
        if (x1 <= x0 || y1 <= y0) {
            return;
        }
        // Block-average pixelate of the region — BGRA, straight alpha;
        // alpha averages with the color so partially-masked boxes stay
        // honest.
        const int stride = frame.Stride();
        std::uint8_t* data = frame.Data();
        for (int by = y0; by < y1; by += block) {
            const int bh = std::min(block, y1 - by);
            for (int bx = x0; bx < x1; bx += block) {
                const int bw = std::min(block, x1 - bx);
                unsigned acc[4] = {0, 0, 0, 0};
                for (int yy = by; yy < by + bh; ++yy) {
                    const std::uint8_t* row =
                        data + static_cast<std::size_t>(yy) * stride;
                    for (int xx = bx; xx < bx + bw; ++xx) {
                        const std::uint8_t* p = row + xx * 4;
                        acc[0] += p[0];
                        acc[1] += p[1];
                        acc[2] += p[2];
                        acc[3] += p[3];
                    }
                }
                const unsigned n =
                    static_cast<unsigned>(bw) * static_cast<unsigned>(bh);
                const std::uint8_t mb =
                    static_cast<std::uint8_t>(acc[0] / n);
                const std::uint8_t mg =
                    static_cast<std::uint8_t>(acc[1] / n);
                const std::uint8_t mr =
                    static_cast<std::uint8_t>(acc[2] / n);
                const std::uint8_t ma =
                    static_cast<std::uint8_t>(acc[3] / n);
                for (int yy = by; yy < by + bh; ++yy) {
                    std::uint8_t* row =
                        data + static_cast<std::size_t>(yy) * stride;
                    for (int xx = bx; xx < bx + bw; ++xx) {
                        std::uint8_t* p = row + xx * 4;
                        p[0] = mb;
                        p[1] = mg;
                        p[2] = mr;
                        p[3] = ma;
                    }
                }
            }
        }
    }

    void ApplyClipEffects(ClipSession& s,
                          const std::vector<ClipEffect>& effects,
                          double clip_time_s) {
        video::Frame& frame = s.frame;
        for (const auto& effect : effects) {
            if (!frame.IsValid()) {
                return;
            }
            const std::string& type = effect.type;
            if (type.rfind("plugin:", 0) == 0 ||
                type.rfind("builtin:", 0) == 0) {
                // External plugin effect (sdk/xer_plugin.h) or
                // host-side builtin — applied in place to the BGRA8
                // frame through the same ABI.
                const PluginDesc* desc = PluginHost::ForEffectType(type);
                // Smart Reframe is geometric, not a pixel kernel: it
                // needs the clip's subject track and the current
                // source time, so RenderClipLayer applies it at the
                // warp stage. Skip it here (its ABI fn is identity
                // anyway).
                if (desc != nullptr && desc->id == Ai::kSmartReframeId) {
                    continue;
                }
                // Tracked Mosaic is also track-driven: its pixelated
                // region comes from Clip::object_track at the frame's
                // source time (the live-reference pin, auto-censor).
                if (desc != nullptr && desc->id == Ai::kTrackMosaicId) {
                    ApplyTrackedMosaic(s, effect, clip_time_s);
                    continue;
                }
                if (desc != nullptr &&
                    frame.Format() == video::PixelFormat::BGRA8) {
                    PluginHost::ApplyVideo(*desc, frame.Data(),
                                           frame.Width(), frame.Height(),
                                           frame.Stride(), effect.params);
                }
                continue;
            }
            if (type.rfind("ocio:", 0) == 0) {
                // Named OCIO profile (color.yaml Profiles: + LUT
                // manager registrations) — LUT or colorspace
                // conversion applied in place. The config loads once
                // per process; unknown profile names and failed
                // transforms leave the frame untouched (identity) —
                // warned at Init, silent here. The copy-out find keeps
                // this thread safe against the LUT manager rescanning
                // profiles on the UI thread mid-frame.
                Color::OcioProfile profile;
                if (Color::FindOcioProfileCopy(type.substr(5),
                                               &profile) &&
                    frame.Format() == video::PixelFormat::BGRA8) {
                    // Export + preview compositors run on different
                    // threads — the one-shot config load must be
                    // synchronized (std::call_once) or a concurrent
                    // reader could observe a half-written ocio_cfg.
                    static std::once_flag ocio_once;
                    static Color::OcioConfig ocio_cfg;
                    static std::filesystem::path ocio_assets_dir;
                    std::call_once(ocio_once, [] {
                        const std::filesystem::path yaml_path(
                            AppRuntime::SharedAssetsConfig()
                                .color_yaml);
                        std::string load_error;
                        Color::LoadOcioConfig(yaml_path, &ocio_cfg,
                                              &load_error);
                        ocio_assets_dir = yaml_path.parent_path();
                    });
                    Color::ApplyProfileTransform(
                        frame.Data(), frame.Width(), frame.Height(),
                        frame.Stride(), profile, ocio_cfg,
                        ocio_assets_dir);
                }
                continue;
            }
            if (type.rfind(kScriptFxPrefix, 0) == 0) {
                // JS-backed effect: zero-copy BGRA view handed to the
                // script's apply(). Runtime lives on this thread;
                // failures warn once and leave the frame untouched.
                const std::string fx_id =
                    type.substr(std::strlen(kScriptFxPrefix));
                JsFxRuntime* fx = JsFx();
                if (fx != nullptr) {
                    std::string fx_error;
                    // ctx.clip_id scopes script-side state to this
                    // clip (parity with the audio dispatch).
                    if (!fx->ApplyVideo(fx_id, &frame, effect.params,
                                        clip_time_s, s.clip->id,
                                        &fx_error)) {
                        Warn("js effect '" + fx_id + "': " +
                             (fx_error.empty() ? "not found"
                                               : fx_error));
                    }
                }
                continue;
            }
            if (KnownEffectTypes().count(type) == 0) {
                continue;   // warned at Init
            }
            if (EffectRequiresSkia(type) && !skia_available) {
                Warn("effect '" + type + "' requires Skia; skipped");
                continue;
            }
            // Shared snapshot buffer for the spatial-remap kernels
            // (glass/lens) — one per render thread, grows once.
            static thread_local std::vector<std::uint8_t> fx_scratch;
            if (type == "brightness_contrast") {
                const int brightness = std::clamp(
                    EffectParamInt(effect, 0, 0), -255, 255);
                const float contrast = std::clamp(
                    EffectParam(effect, 1, 1.0f), 0.0f, 4.0f);
                video::VideoEffects::BrightnessContrast(frame, brightness,
                                                        contrast);
            } else if (type == "grayscale") {
                video::VideoEffects::Grayscale(frame);
            } else if (type == "box_blur") {
                video::VideoEffects::BoxBlur(
                    frame, std::max(1, EffectParamInt(effect, 0, 4)));
            } else if (type == "gaussian_blur") {
                video::VideoEffects::GaussianBlur(
                    frame,
                    std::max(0.0, EffectParamDouble(effect, 0, 2.0)));
            } else if (type == "flip_h") {
                video::VideoEffects::FlipH(frame);
            } else if (type == "flip_v") {
                video::VideoEffects::FlipV(frame);
            } else if (type == "sharpen") {
                video::VideoEffects::Sharpen(
                    frame, EffectParamDouble(effect, 0, 1.5));
            } else if (type == "edge_detect") {
                video::VideoEffects::EdgeDetect(frame);
            } else if (type == "emboss") {
                video::VideoEffects::Emboss(frame);
            } else if (type == "vignette") {
                video::VideoEffects::Vignette(
                    frame, EffectParamDouble(effect, 0, 0.5),
                    EffectParamDouble(effect, 1, 0.7));
            } else if (type == "chromatic_aberration") {
                video::VideoEffects::ChromaticAberration(
                    frame, EffectParamDouble(effect, 0, 2.0));
            } else if (type == "pixelate") {
                video::VideoEffects::Pixelate(
                    frame, std::max(1, EffectParamInt(effect, 0, 8)));
            } else if (type == "oil_paint") {
                video::VideoEffects::OilPaint(
                    frame, std::max(1, EffectParamInt(effect, 0, 4)));
            } else if (type == "motion_blur") {
                video::VideoEffects::MotionBlur(
                    frame, EffectParamDouble(effect, 0, 0.0),
                    std::max(0, EffectParamInt(effect, 1, 10)));
            } else if (type == "chroma_key") {
                // {key_r, key_g, key_b} in 0-255, {tolerance,
                // softness} in RGB-distance units (max ~441).
                const double kr = std::clamp(
                    EffectParamDouble(effect, 0, 0.0), 0.0, 255.0);
                const double kg = std::clamp(
                    EffectParamDouble(effect, 1, 255.0), 0.0, 255.0);
                const double kb = std::clamp(
                    EffectParamDouble(effect, 2, 0.0), 0.0, 255.0);
                const double tolerance = std::clamp(
                    EffectParamDouble(effect, 3, 120.0), 0.0, 600.0);
                const double softness = std::clamp(
                    EffectParamDouble(effect, 4, 40.0), 0.0, 600.0);
                video::VideoEffects::ChromaKey(frame, kr, kg, kb,
                                               tolerance, softness);
            } else if (type == "color_balance") {
                // {lift_r,lift_g,lift_b} in [-1,1] (scaled x255),
                // {gain_r,gain_g,gain_b} in [0,4].
                double lift[3];
                double gain[3];
                for (int c = 0; c < 3; ++c) {
                    lift[c] = std::clamp(
                        EffectParamDouble(
                            effect, static_cast<std::size_t>(c), 0.0),
                        -1.0, 1.0);
                    gain[c] = std::clamp(
                        EffectParamDouble(
                            effect, static_cast<std::size_t>(c + 3),
                            1.0),
                        0.0, 4.0);
                }
                video::VideoEffects::ColorBalance(frame, lift, gain);
            } else if (type == "posterize") {
                video::VideoEffects::Posterize(
                    frame,
                    std::clamp(EffectParamInt(effect, 0, 4), 2, 256));
            } else if (type == "lens_sphere") {
                // {radius frac of min dim, bend 0..2, chroma px,
                //  edge darken 0..1, specular 0..1} — crystal ball:
                // bend past ~1.2 flips the scene like a real ball.
                const double radius = std::clamp(
                    EffectParamDouble(effect, 0, 0.45), 0.05, 1.0) *
                    std::min(frame.Width(), frame.Height()) * 0.5;
                const double bend = std::clamp(
                    EffectParamDouble(effect, 1, 1.4), 0.0, 2.5);
                const double chroma = std::clamp(
                    EffectParamDouble(effect, 2, 3.0), 0.0, 24.0);
                const double edge = std::clamp(
                    EffectParamDouble(effect, 3, 0.35), 0.0, 1.0);
                const double spec = std::clamp(
                    EffectParamDouble(effect, 4, 0.6), 0.0, 1.0);
                if (preview_quality_ < 1.0) {
                    ApplyReduced(&frame, [&](video::Frame* lo) {
                        const double rs =
                            static_cast<double>(lo->Width()) /
                            frame.Width();
                        ApplySphereRefraction(
                            lo, lo->Width() * 0.5, lo->Height() * 0.5,
                            radius * rs, bend, chroma * rs, edge, spec,
                            &fx_scratch);
                    });
                } else {
                    ApplySphereRefraction(
                        &frame, frame.Width() * 0.5,
                        frame.Height() * 0.5, radius, bend, chroma, edge,
                        spec, &fx_scratch);
                }
            } else if (type == "magnifier") {
                // {radius frac, zoom 1..4, chroma px} — thin-lens
                // bulge: bend = 1 - 1/zoom keeps center undistorted.
                const double zoom = std::clamp(
                    EffectParamDouble(effect, 1, 1.8), 1.0, 4.0);
                const double radius = std::clamp(
                    EffectParamDouble(effect, 0, 0.3), 0.05, 1.0) *
                    std::min(frame.Width(), frame.Height()) * 0.5;
                const double chroma = std::clamp(
                    EffectParamDouble(effect, 2, 2.0), 0.0, 24.0);
                if (preview_quality_ < 1.0) {
                    ApplyReduced(&frame, [&](video::Frame* lo) {
                        const double rs =
                            static_cast<double>(lo->Width()) /
                            frame.Width();
                        ApplySphereRefraction(
                            lo, lo->Width() * 0.5, lo->Height() * 0.5,
                            radius * rs, 1.0 - 1.0 / zoom, chroma * rs,
                            0.15, 0.4, &fx_scratch);
                    });
                } else {
                    ApplySphereRefraction(
                        &frame, frame.Width() * 0.5,
                        frame.Height() * 0.5, radius, 1.0 - 1.0 / zoom,
                        chroma, 0.15, 0.4, &fx_scratch);
                }
            } else if (type == "ribbed_glass") {
                // {period px, amplitude px, axis 0=vert|1=horiz,
                //  softness 0..1} — fluted/reeded pane.
                const double period = std::clamp(
                    EffectParamDouble(effect, 0, 24.0), 2.0, 400.0);
                const double amplitude = std::clamp(
                    EffectParamDouble(effect, 1, 6.0), 0.0, 80.0);
                const bool horizontal = EffectParamInt(effect, 2, 0) != 0;
                const double softness = std::clamp(
                    EffectParamDouble(effect, 3, 0.5), 0.0, 1.0);
                if (preview_quality_ < 1.0) {
                    ApplyReduced(&frame, [&](video::Frame* lo) {
                        const double rs =
                            static_cast<double>(lo->Width()) /
                            frame.Width();
                        ApplyRibbedGlassPixels(
                            lo, std::max(period * rs, 2.0),
                            amplitude * rs, horizontal, softness,
                            &fx_scratch);
                    });
                } else {
                    ApplyRibbedGlassPixels(&frame, period, amplitude,
                                           horizontal, softness,
                                           &fx_scratch);
                }
            } else if (type == "bokeh_dof") {
                // {mode 0=band|1=radial, cx, cy, band, falloff,
                //  max radius px, highlight boost} — depth-of-field:
                // CoC grows with distance from the focus zone.
                const bool radial = EffectParamInt(effect, 0, 1) != 0;
                const double fx_cx = EffectParamDouble(effect, 1, 0.5);
                const double fx_cy = EffectParamDouble(effect, 2, 0.5);
                const double band = std::clamp(
                    EffectParamDouble(effect, 3, 0.12), 0.0, 1.0);
                const double falloff = std::clamp(
                    EffectParamDouble(effect, 4, 0.3), 0.01, 1.0);
                const double max_radius = std::clamp(
                    EffectParamDouble(effect, 5, 18.0), 1.0, 64.0);
                const double boost = std::clamp(
                    EffectParamDouble(effect, 6, 3.0), 0.0, 16.0);
                if (preview_quality_ < 1.0) {
                    ApplyReduced(&frame, [&](video::Frame* lo) {
                        const double rs =
                            static_cast<double>(lo->Width()) /
                            frame.Width();
                        ApplyBokehDOFPixels(
                            lo, radial, fx_cx * lo->Width(),
                            fx_cy * lo->Height(), band, falloff,
                            max_radius * rs, boost, &fx_scratch);
                    });
                } else {
                    ApplyBokehDOFPixels(
                        &frame, radial, fx_cx * frame.Width(),
                        fx_cy * frame.Height(), band, falloff,
                        max_radius, boost, &fx_scratch);
                }
            } else if (type == "bokeh_blur") {
                // {radius px, highlight boost} — uniform aperture
                // discs everywhere (dreamy defocus).
                // band=-1 forces (dist-band)/falloff >= 1 everywhere
                // → constant CoC = radius, i.e. uniform aperture blur.
                const double radius = std::clamp(
                    EffectParamDouble(effect, 0, 10.0), 1.0, 64.0);
                const double boost = std::clamp(
                    EffectParamDouble(effect, 1, 2.5), 0.0, 16.0);
                if (preview_quality_ < 1.0) {
                    ApplyReduced(&frame, [&](video::Frame* lo) {
                        const double rs =
                            static_cast<double>(lo->Width()) /
                            frame.Width();
                        ApplyBokehDOFPixels(
                            lo, true, 0.0, 0.0, -1.0, 1.0, radius * rs,
                            boost, &fx_scratch);
                    });
                } else {
                    ApplyBokehDOFPixels(&frame, true, 0.0, 0.0, -1.0,
                                        1.0, radius, boost,
                                        &fx_scratch);
                }
            }
        }
    }

    // First clip effect resolving to the Smart Reframe builtin — the
    // only geometry effect; RenderClipLayer applies it at the warp.
    static const ClipEffect* FindSmartReframe(
        const std::vector<ClipEffect>& effects) {
        for (const ClipEffect& e : effects) {
            if (const PluginDesc* d = PluginHost::ForEffectType(e.type);
                d != nullptr && d->id == Ai::kSmartReframeId) {
                return &e;
            }
        }
        return nullptr;
    }

    // Warps s.frame into s.layer, folding Smart Reframe in: when the
    // clip carries the effect, the tracked aspect-locked crop window
    // (clip.reframe @ track_t, SOURCE seconds) becomes the warp's
    // source region and the effective transform is composed over a
    // cover-fit mapping — crop scaled to fill the canvas at user
    // scale 1, with clip scale multiplying it and clip offsets panning
    // on top. Without the effect the full frame and the effective
    // transform pass through unchanged.
    bool WarpSessionFrame(ClipSession& s, const Clip& clip,
                          const Transform& effective, double track_t,
                          std::string* error_out) {
        Ai::ReframeWindow region{
            0.0, 0.0, static_cast<double>(s.frame.Width()),
            static_cast<double>(s.frame.Height())};
        Transform warp = effective;
        if (const ClipEffect* fx = FindSmartReframe(clip.effects);
            fx != nullptr && s.frame.IsValid()) {
            const double ratio =
                EffectParamDouble(*fx, 0, 9.0 / 16.0);
            const double smooth = EffectParamDouble(*fx, 1, 0.5);
            region = Ai::ReframeWindowAt(
                clip.reframe, track_t, s.frame.Width(),
                s.frame.Height(), ratio, smooth);
            if (region.w > 0.0 && region.h > 0.0) {
                const double cover =
                    std::max(canvas_w / region.w, canvas_h / region.h);
                warp.scale = static_cast<float>(effective.scale * cover);
                warp.offset_x = static_cast<float>(
                    effective.offset_x +
                    (canvas_w - region.w * cover) / (2.0 * canvas_w));
                warp.offset_y = static_cast<float>(
                    effective.offset_y +
                    (canvas_h - region.h * cover) / (2.0 * canvas_h));
            }
        }
        if (!WarpClipFrameRegion(s.frame, region.x, region.y, region.w,
                                 region.h, warp, canvas_w, canvas_h,
                                 &s.layer)) {
            return Fail(error_out, "clip warp failed");
        }
        return true;
    }

    // ---- per-clip layer ---------------------------------------------

    // Renders one clip's canvas-sized layer at timeline time t.
    // Returns false only on fatal errors (open/decode/allocation);
    // no-video and invisible clips leave a transparent layer and
    // return true.
    bool RenderClipLayer(ClipSession& s, double t,
                         std::string* error_out) {
        const Clip& clip = *s.clip;
        // Canvas-size layer — allocate once per session, content is
        // rewritten (warp) or cleared (early-outs) on every call.
        if (s.layer.Width() != canvas_w || s.layer.Height() != canvas_h ||
            s.layer.Format() != video::PixelFormat::BGRA8) {
            s.layer.Release();
            if (!s.layer.Allocate(canvas_w, canvas_h,
                                  video::PixelFormat::BGRA8)) {
                return Fail(error_out, "clip layer allocation failed");
            }
        }
        // Effective per-frame transform: keyframe-animated properties
        // override the static transform fields at clip-local time
        // u = t - timeline_start — NOT scaled by playback_rate (keys
        // are timeline-local like Premiere).
        const double u = t - clip.timeline_start;
        const Transform effective = EffectiveTransformAt(clip, u);
        // Invisible clips skip decode entirely — the layer stays
        // transparent (cheap bbox optimization). Uses the EFFECTIVE
        // transform so animated opacity/scale still gates decode.
        if (effective.opacity <= 0.0f || effective.scale <= 0.0f) {
            ClearFrame(&s.layer);
            s.memo_valid = false;
            return true;
        }
        if (clip.is_adjustment) {
            // Adjustment output depends on the canvas composited BELOW
            // it — not expressible in the cache key. Never memoized or
            // frame-cached.
            s.memo_valid = false;
            return RenderAdjustmentLayer(s, clip, effective, u,
                                         error_out);
        }

        // ---- effect-chain cache / last-layer memo -------------------
        // Key material is computed BEFORE any decode: a frame-cache hit
        // skips decoder positioning AND the whole chain, a memo hit
        // also skips the warp. Keys cover every clip-space input —
        // param edits, source swaps, mask/transform changes and
        // quality-tier flips all miss naturally (see
        // effect_frame_cache.h for the eligibility rules).
        const bool generated = IsGeneratedSource(clip.source);
        // src_t = in_point + (t - timeline_start) * playback_rate —
        // identical math to the decode branch below (handle regions
        // read into the source, clamped to [0, media_duration] once
        // the decoder reports one; EOF covers the unknown case).
        double src_t = 0.0;
        if (!generated) {
            const double rate =
                std::isfinite(clip.playback_rate) &&
                        clip.playback_rate > 0.0
                    ? clip.playback_rate
                    : 1.0;
            src_t = clip.in_point + (t - clip.timeline_start) * rate;
            if (src_t < 0.0) {
                src_t = 0.0;
            }
            if (s.media_duration > 0.0 && src_t > s.media_duration) {
                src_t = s.media_duration;
            }
        }
        // Generated clips are (currently) time-invariant but the API
        // takes u — key on it anyway so a future animated source can
        // never serve stale.
        const std::int64_t time_us =
            QuantizeToUs(generated ? u : src_t);
        if (s.chain_cacheable < 0) {
            s.chain_cacheable = ChainCacheable(clip.effects) ? 1 : 0;
        }
        const bool pure_chain = s.chain_cacheable == 1;
        const std::uint64_t state_hash =
            ClipStateHash(clip, preview_quality_, canvas_w, canvas_h,
                          s.decode_path);
        const std::uint64_t memo_hash =
            MemoHash(state_hash, time_us, u, effective);
        if (pure_chain && s.memo_valid && s.memo_hash == memo_hash) {
            return true;   // identical inputs — s.layer still holds it
        }

        // The frame cache pays for itself only when the chain does
        // real work — an empty effect+mask chain is a memcpy, cheaper
        // than the lookup+store copy.
        const bool cacheable =
            frame_cache != nullptr && pure_chain &&
            (!clip.effects.empty() || !clip.masks.empty());
        EffectFrameCache::Key ckey;
        if (cacheable) {
            ckey.clip_id = clip.id;
            ckey.source_time_us = time_us;
            ckey.state_hash = state_hash;
            if (frame_cache->Lookup(ckey, &s.frame)) {
                if (!WarpSessionFrame(s, clip, effective,
                                      generated ? u : src_t,
                                      error_out)) {
                    return false;
                }
                s.memo_hash = memo_hash;
                s.memo_valid = true;
                return true;
            }
        }

        // Miss path — the pre-cache pipeline, verbatim.
        bool serve_honest = true;
        if (generated) {
            // Generated clips (matte/bars/text) never open a decoder:
            // the intrinsic frame is produced procedurally — matte
            // and bars at canvas size, text at its measured size —
            // then effects/masks/warp/blend apply exactly like they
            // do on decoded media. Regenerated every call so the
            // effect chain always starts from pristine pixels (it
            // destroys s.frame in place).
            if (!GenerateSourceFrame(clip, u, canvas_w, canvas_h,
                                     &s.frame)) {
                return Fail(error_out, "clip '" + clip.id +
                                       "': generated source failed");
            }
        } else {
            const FrameSlot* slot = nullptr;
            const EnsureResult result =
                EnsureFrame(s, src_t, &slot, error_out);
            if (result == EnsureResult::kError) {
                return false;
            }
            if (result == EnsureResult::kNoContent) {
                ClearFrame(&s.layer);
                s.memo_valid = false;
                return true;
            }
            // Materialize the selected frame (slot buffers hold the
            // pristine decode; s.frame is the per-call scratch the
            // effect chain may destroy — it is re-copied next call).
            if (s.frame.Width() != slot->width ||
                s.frame.Height() != slot->height ||
                s.frame.Format() != video::PixelFormat::BGRA8) {
                s.frame.Release();
                if (!s.frame.Allocate(slot->width, slot->height,
                                      video::PixelFormat::BGRA8)) {
                    return Fail(error_out,
                                "clip frame allocation failed");
                }
            }
            std::memcpy(s.frame.Data(), slot->pixels.data(),
                        slot->pixels.size());
            // Honest-serve gate: only cache when the served frame is
            // plausibly the nearest decode for src_t (~1.5 source
            // frames). A backward-scrub serve can pick a slot far from
            // the target — storing it under this key would poison
            // later honest requests (the documented stale-frame path).
            const double tolerance =
                std::max(1.5 / s.fps, 0.002);
            serve_honest = std::fabs(slot->pts - src_t) <= tolerance;
        }
        ApplyClipEffects(s, clip.effects, u);
        // Masks multiply the clip frame's alpha AFTER the effect
        // chain and BEFORE the warp — they are defined in clip-frame
        // fractions, so warping first would smear the coordinates.
        if (!clip.masks.empty() &&
            !ApplyMaskPixels(&s.frame, clip.masks)) {
            return Fail(error_out, "clip mask apply failed");
        }
        if (cacheable && serve_honest) {
            frame_cache->Store(ckey, s.frame);
        }
        // Smart Reframe applies here: the tracked crop window becomes
        // the warp's source region (see WarpSessionFrame).
        if (!WarpSessionFrame(s, clip, effective, generated ? u : src_t,
                              error_out)) {
            return false;
        }
        if (pure_chain) {
            s.memo_hash = memo_hash;
            s.memo_valid = true;
        }
        return true;
    }

    // Premiere-style adjustment layer: the clip owns no pixels — its
    // effect+mask chain runs on a snapshot of the already-composited
    // canvas (everything stacked BELOW its track at this instant —
    // tracks composite bottom->top and this method runs inside that
    // loop), then the result is written back through the clip's
    // transform rect. This is a REGION crop, not a resample: canvas
    // pixel (x,y) inside the footprint takes the effected value of
    // (x,y), so transform.scale/rotation move the coverage only and
    // the underlying pixels are never stretched. The returned layer
    // then rides the normal blend-mode/opacity composite like any
    // other clip layer.
    bool RenderAdjustmentLayer(ClipSession& s, const Clip& clip,
                               const Transform& effective,
                               double clip_time_s,
                               std::string* error_out) {
        // Snapshot the current canvas into the per-session scratch —
        // ApplyClipEffects destroys s.frame in place, so this copy
        // must happen every call (same contract as generated clips).
        if (s.frame.Width() != canvas_w || s.frame.Height() != canvas_h ||
            s.frame.Format() != video::PixelFormat::BGRA8) {
            s.frame.Release();
            if (!s.frame.Allocate(canvas_w, canvas_h,
                                  video::PixelFormat::BGRA8)) {
                return Fail(error_out,
                            "adjustment snapshot allocation failed");
            }
        }
        std::memcpy(s.frame.Data(), canvas.Data(), canvas.DataSize());
        ApplyClipEffects(s, clip.effects, clip_time_s);
        // Masks behave exactly as on normal clips: clip-frame
        // fractions — and the adjustment "clip frame" IS the canvas,
        // so fractions map 1:1 onto canvas space.
        if (!clip.masks.empty() &&
            !ApplyMaskPixels(&s.frame, clip.masks)) {
            return Fail(error_out, "clip mask apply failed");
        }

        // Coverage footprint: the same rect geometry WarpClipFrame
        // computes for a canvas-size source — (canvas_w*scale x
        // canvas_h*scale) with its top-left at
        // (offset_x*canvas_w, offset_y*canvas_h), rotated
        // rotation_deg about its center.
        const double scale = static_cast<double>(effective.scale);
        const double dw = canvas_w * scale;
        const double dh = canvas_h * scale;
        const double cx =
            effective.offset_x * canvas_w + dw * 0.5;
        const double cy =
            effective.offset_y * canvas_h + dh * 0.5;
        const double theta =
            static_cast<double>(effective.rotation_deg) * kPi / 180.0;
        const double cos_t = std::cos(theta);
        const double sin_t = std::sin(theta);
        const double half_dw = dw * 0.5;
        const double half_dh = dh * 0.5;
        const double opacity = std::clamp(
            static_cast<double>(effective.opacity), 0.0, 1.0);

        // Integer bbox of the rotated rect (forward-transform the
        // four corners, floor/ceil, clamp) — same math as the warp.
        double min_x = std::numeric_limits<double>::infinity();
        double min_y = std::numeric_limits<double>::infinity();
        double max_x = -std::numeric_limits<double>::infinity();
        double max_y = -std::numeric_limits<double>::infinity();
        for (int i = 0; i < 4; ++i) {
            const double ux = (i & 1) != 0 ? half_dw : -half_dw;
            const double uy = (i & 2) != 0 ? half_dh : -half_dh;
            const double vx = ux * cos_t - uy * sin_t;
            const double vy = ux * sin_t + uy * cos_t;
            min_x = std::min(min_x, cx + vx);
            min_y = std::min(min_y, cy + vy);
            max_x = std::max(max_x, cx + vx);
            max_y = std::max(max_y, cy + vy);
        }
        ClearFrame(&s.layer);
        if (min_x > max_x || min_y > max_y) {
            return true;   // fully offscreen — transparent layer
        }
        const int bx0 = std::clamp(
            static_cast<int>(std::floor(min_x)), 0, canvas_w - 1);
        const int bx1 = std::clamp(
            static_cast<int>(std::ceil(max_x)), 0, canvas_w - 1);
        const int by0 = std::clamp(
            static_cast<int>(std::floor(min_y)), 0, canvas_h - 1);
        const int by1 = std::clamp(
            static_cast<int>(std::ceil(max_y)), 0, canvas_h - 1);
        if (bx1 < bx0 || by1 < by0) {
            return true;
        }

        const std::uint8_t* spx = s.frame.Data();
        std::uint8_t* dpx = s.layer.Data();
        for (int y = by0; y <= by1; ++y) {
            for (int x = bx0; x <= bx1; ++x) {
                // Inside test in the rect's rotated space — identical
                // to WarpClipFrame's inverse map, minus the sampling.
                const double vx = x + 0.5 - cx;
                const double vy = y + 0.5 - cy;
                const double ux = vx * cos_t + vy * sin_t;
                const double uy = -vx * sin_t + vy * cos_t;
                if (std::fabs(ux) > half_dw ||
                    std::fabs(uy) > half_dh) {
                    continue;
                }
                const std::size_t i =
                    (static_cast<std::size_t>(y) * canvas_w + x) * 4u;
                // Straight alpha: RGB passes through, alpha scales.
                dpx[i + 0] = spx[i + 0];
                dpx[i + 1] = spx[i + 1];
                dpx[i + 2] = spx[i + 2];
                dpx[i + 3] = Clamp8(spx[i + 3] * opacity);
            }
        }
        return true;
    }

    // ---- degenerate overlap ------------------------------------------

    void WarnAmbiguousOnce(double t) {
        if (warned_ambiguous) {
            return;
        }
        warned_ambiguous = true;
        Warn("ambiguous clip overlap at t=" + std::to_string(t) +
             " — picked the latest-starting candidate");
    }

    // >=2 candidates with no matching transition (invalid/degenerate
    // project — overlapping ext windows). Pick deterministically:
    // nominal-active candidate with the latest start; else latest
    // start <= t; else latest ext_start (all candidates start after t).
    ClipSession* PickDegenerate(std::vector<ClipSession*>& candidates,
                                double t) {
        ClipSession* best_nominal = nullptr;
        ClipSession* best_start = nullptr;
        ClipSession* best_ext = nullptr;
        for (ClipSession* s : candidates) {
            const Clip& clip = *s->clip;
            if (clip.timeline_start <= t && t < ClipEndSeconds(clip) &&
                (best_nominal == nullptr ||
                 clip.timeline_start >
                     best_nominal->clip->timeline_start)) {
                best_nominal = s;
            }
            if (clip.timeline_start <= t &&
                (best_start == nullptr ||
                 clip.timeline_start > best_start->clip->timeline_start)) {
                best_start = s;
            }
            if (best_ext == nullptr || s->ext_start > best_ext->ext_start) {
                best_ext = s;
            }
        }
        if (best_nominal != nullptr) {
            return best_nominal;
        }
        if (best_start != nullptr) {
            return best_start;
        }
        return best_ext;   // candidates is non-empty by contract
    }

    // ---- frame render -------------------------------------------------

    bool RenderFrameAt(double t, std::vector<std::uint8_t>* out_bgra,
                       std::string* error_out) {
        if (!initialized) {
            return Fail(error_out, "compositor not initialized");
        }
        if (out_bgra == nullptr) {
            return Fail(error_out, "out_bgra is required");
        }
        if (cancelled.load(std::memory_order_relaxed)) {
            return Fail(error_out, "export cancelled");
        }
        const double tc = std::clamp(t, 0.0, duration);

        if (!canvas.IsValid() || canvas.Width() != canvas_w ||
            canvas.Height() != canvas_h ||
            canvas.Format() != video::PixelFormat::BGRA8) {
            canvas.Release();
            if (!canvas.Allocate(canvas_w, canvas_h,
                                 video::PixelFormat::BGRA8)) {
                return Fail(error_out, "canvas allocation failed");
            }
        }
        FillBackground(&canvas, project_.bg_color);

        // Tracks bottom->top in project order; hidden tracks contribute
        // neither video nor (implicitly) audio.
        for (const int track_index : video_track_indices) {
            const Track& track =
                project_.tracks[static_cast<std::size_t>(track_index)];
            if (track.hidden) {
                continue;
            }
            candidates_scratch.clear();
            for (auto& entry : sessions) {
                ClipSession& s = entry.second;
                if (s.track_index == track_index && InWindow(s, tc)) {
                    candidates_scratch.push_back(&s);
                }
            }
            if (candidates_scratch.empty()) {
                continue;
            }

            const video::Frame* layer = nullptr;
            BlendMode layer_mode = BlendMode::Normal;
            if (candidates_scratch.size() == 1) {
                ClipSession* s = candidates_scratch.front();
                if (!RenderClipLayer(*s, tc, error_out)) {
                    return false;
                }
                layer = &s->layer;
                layer_mode = s->clip->blend_mode;
            } else {
                // >=2 candidates: a stored transition whose window
                // contains t AND whose endpoints are both candidates
                // wins — including during the window's FIRST HALF when
                // the from-clip is still nominally active (the
                // transition pair takes precedence over nominal).
                const Transition* transition = nullptr;
                ClipSession* from_session = nullptr;
                ClipSession* to_session = nullptr;
                for (const Transition* candidate : valid_transitions) {
                    const Clip* from_clip =
                        FindClip(project_, candidate->from_clip);
                    const Clip* to_clip =
                        FindClip(project_, candidate->to_clip);
                    if (from_clip == nullptr || to_clip == nullptr) {
                        continue;
                    }
                    const double cut = ClipEndSeconds(*from_clip);
                    if (std::fabs(to_clip->timeline_start - cut) >
                        kAdjacencyEpsilon) {
                        continue;
                    }
                    const double half = candidate->duration * 0.5;
                    if (tc < cut - half || tc >= cut + half) {
                        continue;
                    }
                    auto from_it = sessions.find(candidate->from_clip);
                    auto to_it = sessions.find(candidate->to_clip);
                    if (from_it == sessions.end() ||
                        to_it == sessions.end() ||
                        from_it->second.track_index != track_index ||
                        to_it->second.track_index != track_index ||
                        !InWindow(from_it->second, tc) ||
                        !InWindow(to_it->second, tc)) {
                        continue;
                    }
                    transition = candidate;
                    from_session = &from_it->second;
                    to_session = &to_it->second;
                    break;   // first stored match wins (deterministic)
                }
                if (transition != nullptr) {
                    if (!RenderClipLayer(*from_session, tc, error_out)) {
                        return false;
                    }
                    if (!RenderClipLayer(*to_session, tc, error_out)) {
                        return false;
                    }
                    const Clip* from_clip =
                        FindClip(project_, transition->from_clip);
                    const Clip* to_clip =
                        FindClip(project_, transition->to_clip);
                    const double cut = ClipEndSeconds(*from_clip);
                    double weight =
                        TransitionWeightAt(tc, *transition, cut);
                    if (transition->type == Transition::Type::Script) {
                        // Script transitions supply the easing curve;
                        // rendering stays the crossfade path. The
                        // linear weight is the progress input; a
                        // missing/failed script keeps it linear.
                        JsFxRuntime* fx = JsFx();
                        if (fx != nullptr) {
                            weight = fx->TransitionWeight(
                                transition->script, weight);
                        }
                    }
                    // Script transitions render through the crossfade
                    // pixel path — the script supplies the curve only.
                    const Transition::Type pixel_type =
                        transition->type == Transition::Type::Script
                            ? Transition::Type::Crossfade
                            : transition->type;
                    if (!ApplyTransitionPixels(
                            pixel_type, weight,
                            from_session->layer, to_session->layer,
                            &transition_scratch)) {
                        return Fail(error_out,
                                    "transition compositing failed");
                    }
                    layer = &transition_scratch;
                    // The merged transition layer is a single clip
                    // stand-in: it carries the blend mode of whichever
                    // endpoint dominates at this weight, so the
                    // composite is continuous at BOTH window edges
                    // (w->0 is `from`, w->1 is `to`).
                    layer_mode =
                        weight < 0.5 || to_clip == nullptr
                            ? from_clip->blend_mode
                            : to_clip->blend_mode;
                } else {
                    WarnAmbiguousOnce(tc);
                    ClipSession* pick =
                        PickDegenerate(candidates_scratch, tc);
                    if (!RenderClipLayer(*pick, tc, error_out)) {
                        return false;
                    }
                    layer = &pick->layer;
                    layer_mode = pick->clip->blend_mode;
                }
            }
            if (layer != nullptr && layer->IsValid()) {
                // Normal keeps the dedicated alpha-over path; every
                // other BlendMode goes through the W3C-style blend
                // composite (identical math for Normal, so no
                // regression).
                if (layer_mode == BlendMode::Normal) {
                    AlphaOverFrame(&canvas, *layer);
                } else if (!ApplyBlendModePixels(layer_mode, &canvas,
                                                 *layer)) {
                    return Fail(error_out, "blend compositing failed");
                }
            }
        }

        CloseFinishedSessions(tc);

        out_bgra->assign(canvas.Data(),
                         canvas.Data() + canvas.DataSize());
        return true;
    }
};

// ------------------------------------------------------------------
// Public API
// ------------------------------------------------------------------

double TransitionWeightAt(double t_seconds,
                          const Transition& transition,
                          double cut_seconds) {
    const double d = transition.duration;
    if (!(d > 0.0)) {
        // Degenerate (non-positive or NaN): w = 1 at/after the cut.
        return t_seconds >= cut_seconds ? 1.0 : 0.0;
    }
    const double w = (t_seconds - (cut_seconds - d * 0.5)) / d;
    return std::clamp(w, 0.0, 1.0);
}

bool ApplyTransitionPixels(Transition::Type type, double weight,
                           const video::Frame& under,
                           const video::Frame& over,
                           video::Frame* out) {
    if (out == nullptr) {
        return false;
    }
    if (under.Format() != video::PixelFormat::BGRA8 ||
        over.Format() != video::PixelFormat::BGRA8 ||
        !under.IsValid() || !over.IsValid()) {
        return false;
    }
    const int width = under.Width();
    const int height = under.Height();
    if (width <= 0 || height <= 0 || over.Width() != width ||
        over.Height() != height) {
        return false;
    }
    if (!out->IsValid() || out->Width() != width ||
        out->Height() != height ||
        out->Format() != video::PixelFormat::BGRA8) {
        out->Release();
        if (!out->Allocate(width, height, video::PixelFormat::BGRA8)) {
            return false;
        }
    }
    const double w = std::clamp(weight, 0.0, 1.0);
    const double iw = 1.0 - w;
    const std::uint8_t* u = under.Data();
    const std::uint8_t* o = over.Data();
    std::uint8_t* d = out->Data();

    if (type == Transition::Type::Crossfade) {
        const int count = width * height;
        for (int i = 0; i < count; ++i) {
            CrossfadePixel(u + i * 4, o + i * 4, w, iw, d + i * 4);
        }
        return true;
    }
    if (IsWipe(type)) {
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                // Start from `under` verbatim, then let `over`
                // alpha-composite on top inside the revealed region.
                d[i + 0] = u[i + 0];
                d[i + 1] = u[i + 1];
                d[i + 2] = u[i + 2];
                d[i + 3] = u[i + 3];
                if (WipeReveals(type, x, y, width, height, w)) {
                    AlphaOverPixel(d + i, o + i);
                }
            }
        }
        return true;
    }
    if (IsSlide(type)) {
        double udx = 0.0, udy = 0.0, odx = 0.0, ody = 0.0;
        SlideOffsets(type, w, width, height, &udx, &udy, &odx, &ody);
        std::uint8_t upx[4] = {0, 0, 0, 0};
        std::uint8_t opx[4] = {0, 0, 0, 0};
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const std::size_t i =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                SampleBilinearBGRA(u, width, height,
                                   x + 0.5 - udx, y + 0.5 - udy,
                                   false, upx);
                SampleBilinearBGRA(o, width, height,
                                   x + 0.5 - odx, y + 0.5 - ody,
                                   false, opx);
                d[i + 0] = upx[0];
                d[i + 1] = upx[1];
                d[i + 2] = upx[2];
                d[i + 3] = upx[3];
                AlphaOverPixel(d + i, opx);
            }
        }
        return true;
    }
    if (type == Transition::Type::DipToBlack ||
        type == Transition::Type::DipToWhite) {
        // Through-solid dip: the first half fades `under` into the
        // solid frame, the second half fades `over` out of it. The
        // solid is opaque, so a faded layer over it collapses to a
        // straight rgb lerp toward the solid and the output alpha is
        // always 255.
        const std::uint8_t solid =
            type == Transition::Type::DipToBlack ? 0 : 255;
        const bool first_half = w < 0.5;
        const std::uint8_t* lay = first_half ? u : o;
        const double fade =
            first_half ? (1.0 - 2.0 * w) : (2.0 * w - 1.0);
        const int count = width * height;
        for (int i = 0; i < count; ++i) {
            const std::uint8_t* lp = lay + i * 4;
            const double sa = (lp[3] / 255.0) * fade;
            std::uint8_t* dp = d + i * 4;
            dp[0] = Clamp8(lp[0] * sa + solid * (1.0 - sa));
            dp[1] = Clamp8(lp[1] * sa + solid * (1.0 - sa));
            dp[2] = Clamp8(lp[2] * sa + solid * (1.0 - sa));
            dp[3] = 255;
        }
        return true;
    }
    if (type == Transition::Type::AdditiveDissolve) {
        // Additive dissolve: straight (non-premultiplied) sum of the
        // weighted contributions on ALL four channels — unlike
        // CrossfadePixel the rgb channels are not alpha-weighted, so
        // semi-transparent layers still add their full color.
        const int count = width * height;
        for (int i = 0; i < count; ++i) {
            for (int c = 0; c < 4; ++c) {
                d[i * 4 + c] =
                    Clamp8(u[i * 4 + c] * iw + o[i * 4 + c] * w);
            }
        }
        return true;
    }
    return false;   // unknown transition type
}

bool ApplyMaskPixels(video::Frame* frame,
                     const std::vector<ClipMask>& masks) {
    if (frame == nullptr || !frame->IsValid() ||
        frame->Format() != video::PixelFormat::BGRA8) {
        return false;
    }
    if (masks.empty()) {
        return true;   // no-op — frame untouched
    }
    const int w = frame->Width();
    const int h = frame->Height();
    std::uint8_t* px = frame->Data();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double coverage = 1.0;
            for (const ClipMask& mask : masks) {
                coverage *=
                    MaskCoverage(mask, w, h, x + 0.5, y + 0.5);
                if (coverage <= 0.0) {
                    break;   // already fully masked out
                }
            }
            // Straight alpha: RGB passes through, A scales.
            std::uint8_t* p =
                px + (static_cast<std::size_t>(y) * w + x) * 4u;
            p[3] = Clamp8(p[3] * coverage);
        }
    }
    return true;
}

bool ApplyBlendModePixels(BlendMode mode, video::Frame* dst,
                          const video::Frame& src) {
    if (dst == nullptr || !dst->IsValid() || !src.IsValid() ||
        dst->Format() != video::PixelFormat::BGRA8 ||
        src.Format() != video::PixelFormat::BGRA8 ||
        dst->Width() != src.Width() ||
        dst->Height() != src.Height()) {
        return false;
    }
    if (mode == BlendMode::Normal || mode == BlendMode::SrcOver) {
        AlphaOverFrame(dst, src);   // identical math, dedicated path
        return true;
    }
    // All named modes route through the shared frame-level blend —
    // W3C Porter-Duff / separable / non-separable compositing in
    // MixSystem::BlendFrames -> Bridge::Skia::BlendModeApply.
    //
    // Intentional semantic change vs the retired loop for the original
    // six modes (Add/Multiply/Screen/Overlay/Darken/Lighten): the old
    // code weighted the blend only by source alpha; the W3C composite
    // also weights by backdrop alpha — identical for opaque layers
    // (the common case), marginally different for translucent ones,
    // matching Skia.
    return video::MixSystem::BlendFrames(*dst, src,
                                         BlendModeSkiaName(mode));
}

bool WarpClipFrame(const video::Frame& src, const Transform& tr,
                   int canvas_w, int canvas_h, video::Frame* out_layer) {
    return WarpClipFrameRegion(
        src, 0.0, 0.0,
        src.IsValid() ? static_cast<double>(src.Width()) : 0.0,
        src.IsValid() ? static_cast<double>(src.Height()) : 0.0, tr,
        canvas_w, canvas_h, out_layer);
}

bool WarpClipFrameRegion(const video::Frame& src, double region_x,
                         double region_y, double region_w,
                         double region_h, const Transform& tr,
                         int canvas_w, int canvas_h,
                         video::Frame* out_layer) {
    if (out_layer == nullptr || !src.IsValid() ||
        src.Format() != video::PixelFormat::BGRA8) {
        return false;
    }
    const int src_w = src.Width();
    const int src_h = src.Height();
    // The sampled window, clamped into the frame.
    const double rx = std::clamp(region_x, 0.0,
                                 static_cast<double>(src_w));
    const double ry = std::clamp(region_y, 0.0,
                                 static_cast<double>(src_h));
    const double rw = std::clamp(
        region_w, 0.0, static_cast<double>(src_w) - rx);
    const double rh = std::clamp(
        region_h, 0.0, static_cast<double>(src_h) - ry);
    if (src_w <= 0 || src_h <= 0 || rw <= 0.0 || rh <= 0.0 ||
        canvas_w <= 0 || canvas_h <= 0) {
        return false;
    }
    if (!out_layer->IsValid() || out_layer->Width() != canvas_w ||
        out_layer->Height() != canvas_h ||
        out_layer->Format() != video::PixelFormat::BGRA8) {
        out_layer->Release();
        if (!out_layer->Allocate(canvas_w, canvas_h,
                                 video::PixelFormat::BGRA8)) {
            return false;
        }
    } else {
        ClearFrame(out_layer);   // fully rewritten below
    }
    const float scale = tr.scale;
    if (scale <= 0.0f) {
        return true;   // degenerate — transparent layer
    }
    const double inv_scale = 1.0 / static_cast<double>(scale);
    const double dw = rw * static_cast<double>(scale);
    const double dh = rh * static_cast<double>(scale);
    const double ox = tr.offset_x * canvas_w;
    const double oy = tr.offset_y * canvas_h;
    const double cx = ox + dw * 0.5;
    const double cy = oy + dh * 0.5;
    const double theta =
        static_cast<double>(tr.rotation_deg) * kPi / 180.0;
    const double cos_t = std::cos(theta);
    const double sin_t = std::sin(theta);
    const double half_dw = dw * 0.5;
    const double half_dh = dh * 0.5;
    const double opacity =
        std::clamp(static_cast<double>(tr.opacity), 0.0, 1.0);

    // Integer bbox of the rotated scaled rect: transform the four
    // corners forward (+theta about the center), floor/ceil, clamp.
    double min_x = std::numeric_limits<double>::infinity();
    double min_y = std::numeric_limits<double>::infinity();
    double max_x = -std::numeric_limits<double>::infinity();
    double max_y = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < 4; ++i) {
        const double ux = (i & 1) != 0 ? half_dw : -half_dw;
        const double uy = (i & 2) != 0 ? half_dh : -half_dh;
        const double vx = ux * cos_t - uy * sin_t;
        const double vy = ux * sin_t + uy * cos_t;
        min_x = std::min(min_x, cx + vx);
        min_y = std::min(min_y, cy + vy);
        max_x = std::max(max_x, cx + vx);
        max_y = std::max(max_y, cy + vy);
    }
    const int bx0 = std::clamp(static_cast<int>(std::floor(min_x)), 0,
                             canvas_w - 1);
    const int bx1 = std::clamp(static_cast<int>(std::ceil(max_x)), 0,
                             canvas_w - 1);
    const int by0 = std::clamp(static_cast<int>(std::floor(min_y)), 0,
                             canvas_h - 1);
    const int by1 = std::clamp(static_cast<int>(std::ceil(max_y)), 0,
                             canvas_h - 1);
    if (bx1 < bx0 || by1 < by0) {
        return true;   // fully offscreen — transparent layer
    }

    const std::uint8_t* src_px = src.Data();
    std::uint8_t* dst_px = out_layer->Data();
    for (int y = by0; y <= by1; ++y) {
        for (int x = bx0; x <= bx1; ++x) {
            // Inverse map: dest pixel center -> scaled-rect space
            // relative to the rotation center, rotate by -theta.
            const double vx = x + 0.5 - cx;
            const double vy = y + 0.5 - cy;
            const double ux = vx * cos_t + vy * sin_t;
            const double uy = -vx * sin_t + vy * cos_t;
            if (std::fabs(ux) > half_dw || std::fabs(uy) > half_dh) {
                continue;   // outside the scaled rect -> transparent
            }
            // Region-local source coords + the window's frame offset.
            const double sx = ux * inv_scale + rw * 0.5 + rx;
            const double sy = uy * inv_scale + rh * 0.5 + ry;
            std::uint8_t spx[4] = {0, 0, 0, 0};
            SampleBilinearBGRA(src_px, src_w, src_h, sx, sy, true, spx);
            std::uint8_t* op =
                dst_px + (static_cast<std::size_t>(y) * canvas_w + x) *
                             4u;
            // Straight alpha: RGB passes through, alpha scales.
            op[0] = spx[0];
            op[1] = spx[1];
            op[2] = spx[2];
            op[3] = Clamp8(spx[3] * opacity);
        }
    }
    return true;
}

Compositor::Compositor() : impl_(std::make_unique<Impl>()) {}

Compositor::~Compositor() = default;

bool Compositor::Init(const Project& project,
                      const CompositorOptions& options,
                      std::string* error_out,
                      std::vector<std::string>* warnings_out) {
    return impl_->Init(project, options, error_out, warnings_out);
}

void Compositor::Shutdown() { impl_->Shutdown(); }

int Compositor::CanvasWidth() const { return impl_->canvas_w; }

int Compositor::CanvasHeight() const { return impl_->canvas_h; }

double Compositor::DurationSeconds() const { return impl_->duration; }

std::size_t Compositor::FrameCount(int fps_num, int fps_den) const {
    if (!impl_->initialized || impl_->duration <= 0.0 || fps_num <= 0 ||
        fps_den <= 0) {
        return 0;
    }
    const double frames =
        impl_->duration * static_cast<double>(fps_num) /
        static_cast<double>(fps_den);
    const auto count = static_cast<std::size_t>(std::llround(frames));
    return count == 0 ? 1 : count;   // duration > 0 -> at least 1 frame
}

bool Compositor::RenderFrameAt(double t,
                               std::vector<std::uint8_t>* out_bgra,
                               std::string* error_out) {
    return impl_->RenderFrameAt(t, out_bgra, error_out);
}

void Compositor::SetPreviewQuality(float scale) {
    impl_->preview_quality_ =
        (std::isfinite(scale) && scale > 0.0f && scale < 1.0f)
            ? static_cast<double>(scale)
            : 1.0;
}

float Compositor::PreviewQuality() const {
    return static_cast<float>(impl_->preview_quality_);
}

EffectFrameCache::Stats Compositor::FrameCacheStats() const {
    return impl_->frame_cache ? impl_->frame_cache->GetStats()
                              : EffectFrameCache::Stats{};
}

const std::vector<std::string>& Compositor::Warnings() const {
    return impl_->warnings;
}

void Compositor::Cancel() {
    impl_->cancelled.store(true, std::memory_order_relaxed);
}

bool Compositor::Cancelled() const {
    return impl_->cancelled.load(std::memory_order_relaxed);
}

}  // namespace XER::VideoEditor
