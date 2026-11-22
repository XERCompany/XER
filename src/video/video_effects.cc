#include "video_effects.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include <cstring>

namespace video {

// BGRA8 ↔ Skia uint32 (MakeColorRGBA(R,G,B,A) = 0xRRGGBBAA)
// The scratch is a per-thread persistent buffer: an effect chain runs
// several Skia-bridged kernels per frame and each one used to heap-
// allocate a fresh w*h*4 vector on the way in (same for the CPU
// scratch buffers below). Capacity is retained across calls, so a
// steady-state chain performs zero allocations. Callers must treat
// the returned buffer as volatile — it is rewritten by the next
// FrameToSkia call on this thread.
static std::vector<uint32_t>& FrameToSkia(const Frame& f) {
    thread_local std::vector<uint32_t> px;
    const int n = f.Width() * f.Height();
    px.resize(static_cast<size_t>(n));
    const uint8_t* d = f.Data();
    for (int i = 0; i < n; ++i)
        px[i] = XER::Bridge::Skia::MakeColorRGBA(d[i*4+2], d[i*4+1], d[i*4+0], d[i*4+3]);
    return px;
}

static void SkiaToFrame(const std::vector<uint32_t>& px, Frame& f) {
    uint8_t* d = f.Data();
    for (int i = 0, n = (int)px.size(); i < n; ++i) {
        uint32_t c = px[i];
        d[i*4+2] = (c>>24) & 0xFF;  // R
        d[i*4+1] = (c>>16) & 0xFF;  // G
        d[i*4+0] = (c>> 8) & 0xFF;  // B
        d[i*4+3] =  c      & 0xFF;  // A
    }
}

void VideoEffects::BrightnessContrast(Frame& frame, int brightness, float contrast) {
    if (!frame.IsValid()) return;
    // Skia doesn’t have a direct brightness/contrast; use CPU with exact coefficients
    int n = frame.Width() * frame.Height();
    uint8_t* px = frame.Data();
    for (int i = 0; i < n; ++i) {
        int idx = i * 4;
        for (int c = 0; c < 3; ++c) {
            float v = (px[idx+c] - 128) * contrast + 128 + brightness;
            px[idx+c] = static_cast<uint8_t>(std::clamp(v, 0.f, 255.f));
        }
    }
}

void VideoEffects::Grayscale(Frame& frame) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        // Skia: set R=G=B to same value via per-pixel blend workaround;
        // easier CPU path but we still convert through Skia to keep pipeline consistent
        auto& px = FrameToSkia(frame);
        for (auto& c : px) {
            uint8_t r = (c >> 24) & 0xFF;
            uint8_t g = (c >> 16) & 0xFF;
            uint8_t b = (c >>  8) & 0xFF;
            uint8_t a =  c        & 0xFF;
            uint8_t luma = static_cast<uint8_t>(0.299f*r + 0.587f*g + 0.114f*b);
            c = XER::Bridge::Skia::MakeColorRGBA(luma, luma, luma, a);
        }
        SkiaToFrame(px, frame);
    } else {
        int n = frame.Width() * frame.Height();
        uint8_t* px = frame.Data();
        for (int i = 0; i < n; ++i) {
            int idx = i * 4;
            uint8_t luma = static_cast<uint8_t>(0.299f*px[idx+2]+0.587f*px[idx+1]+0.114f*px[idx+0]);
            px[idx+0] = px[idx+1] = px[idx+2] = luma;
        }
    }
}

void VideoEffects::BoxBlur(Frame& frame, int radius) {
    if (!frame.IsValid() || radius <= 0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterBlur(&px, frame.Width(), frame.Height(),
                                          radius, radius);
        SkiaToFrame(px, frame);
    } else {
        // CPU box blur (horizontal pass only for speed)
        int w = frame.Width(), h = frame.Height();
        thread_local std::vector<uint8_t> tmp;
        tmp.resize(static_cast<size_t>(w) * h * 4);
        const uint8_t* src = frame.Data();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                int r=0,g=0,b=0,a=0,cnt=0;
                for (int k=-radius;k<=radius;++k) {
                    int cx = std::clamp(x+k, 0, w-1);
                    int idx = (y*w+cx)*4;
                    b+=src[idx]; g+=src[idx+1]; r+=src[idx+2]; a+=src[idx+3]; ++cnt;
                }
                int idx=(y*w+x)*4;
                tmp[idx]=static_cast<uint8_t>(b/cnt); tmp[idx+1]=static_cast<uint8_t>(g/cnt);
                tmp[idx+2]=static_cast<uint8_t>(r/cnt); tmp[idx+3]=static_cast<uint8_t>(a/cnt);
            }
        }
        std::memcpy(frame.Data(), tmp.data(), w*h*4);
    }
}

void VideoEffects::GaussianBlur(Frame& frame, double sigma) {
    if (!frame.IsValid() || sigma <= 0.0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterGaussianBlur(&px, frame.Width(), frame.Height(),
                                                   sigma, sigma);
        SkiaToFrame(px, frame);
    } else {
        BoxBlur(frame, static_cast<int>(sigma * 1.5));
    }
}

void VideoEffects::Sharpen(Frame& frame, double amount) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterSharpen(&px, frame.Width(), frame.Height(),
                                             amount, 1.0, 0);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::EdgeDetect(Frame& frame) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterEdgeDetect(&px, frame.Width(), frame.Height());
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::Emboss(Frame& frame) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterEmboss(&px, frame.Width(), frame.Height(), 315.0, 1.5);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::Vignette(Frame& frame, double strength, double radius) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterVignette(&px, frame.Width(), frame.Height(),
                                              strength, radius);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::ChromaticAberration(Frame& frame, double offset) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        int off = static_cast<int>(offset);
        XER::Bridge::Skia::FilterChromaticAberration(&px, frame.Width(), frame.Height(),
                                                         off, 0, -off, 0);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::Pixelate(Frame& frame, int block_size) {
    if (!frame.IsValid() || block_size <= 0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterPixelate(&px, frame.Width(), frame.Height(), block_size);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::OilPaint(Frame& frame, int radius) {
    if (!frame.IsValid() || radius <= 0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterOilPaint(&px, frame.Width(), frame.Height(), radius, 20);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::MotionBlur(Frame& frame, double angle, int distance) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterMotionBlur(&px, frame.Width(), frame.Height(),
                                                angle, distance);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::FlipH(Frame& frame) {
    if (!frame.IsValid()) return;
    int w = frame.Width(), h = frame.Height();
    uint8_t* px = frame.Data();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w/2; ++x) {
            int a=(y*w+x)*4, b=(y*w+w-1-x)*4;
            for (int c=0;c<4;++c) std::swap(px[a+c], px[b+c]);
        }
}

void VideoEffects::FlipV(Frame& frame) {
    if (!frame.IsValid()) return;
    int w = frame.Width(), h = frame.Height();
    uint8_t* px = frame.Data();
    thread_local std::vector<uint8_t> row(static_cast<size_t>(w) * 4);
    if (row.size() < static_cast<size_t>(w) * 4) {
        row.resize(static_cast<size_t>(w) * 4);
    }
    for (int y = 0; y < h/2; ++y) {
        int a=y*w*4, b=(h-1-y)*w*4;
        std::memcpy(row.data(), px+a, w*4);
        std::memcpy(px+a,       px+b, w*4);
        std::memcpy(px+b,  row.data(), w*4);
    }
}

void VideoEffects::ChromaKey(Frame& frame, double key_r, double key_g,
                             double key_b, double tolerance,
                             double softness) {
    if (!frame.IsValid()) return;
    const int w = frame.Width();
    const int h = frame.Height();
    const int count = w * h;
    const double lo = tolerance - softness;
    const double hi = tolerance + softness;

    if (XER::Bridge::Skia::IsAvailable()) {
        // Route through the bridge's mask ops. Semantic delta: the
        // bridge's MaskFromColorRange measures L1 RGB distance (0..765)
        // where the CPU path below uses Euclidean (0..~441). The window
        // is scaled by ~1.5 — the L1/L2 ratio for uniformly-directed
        // channel differences — so tolerance/softness keep roughly the
        // same meaning; the smoothstep ramp is then recovered by
        // remapping the mask (which encodes distance) to "keep" alpha.
        constexpr double kL1Scale = 1.5;
        const double lo_l1 = lo * kL1Scale;
        const double hi_l1 = hi * kL1Scale;
        const int mask_tol = std::max(1, static_cast<int>(std::ceil(hi_l1)));
        auto& px = FrameToSkia(frame);
        std::vector<uint8_t> mask;
        const uint32_t seed = XER::Bridge::Skia::MakeColorRGBA(
            static_cast<int>(std::clamp(key_r, 0.0, 255.0)),
            static_cast<int>(std::clamp(key_g, 0.0, 255.0)),
            static_cast<int>(std::clamp(key_b, 0.0, 255.0)), 255);
        if (XER::Bridge::Skia::MaskFromColorRange(px, w, h, seed, mask_tol,
                                                  &mask)) {
            for (uint8_t& m : mask) {
                // m == 0 -> dist >= mask_tol -> fully keep; otherwise
                // invert the ramp to recover the L1 distance.
                double keep = 1.0;
                if (m != 0) {
                    const double dist =
                        (255.0 - m) / 255.0 * mask_tol;
                    if (hi_l1 <= lo_l1) {
                        keep = dist > lo_l1 ? 1.0 : 0.0;
                    } else {
                        const double t = std::clamp(
                            (dist - lo_l1) / (hi_l1 - lo_l1), 0.0, 1.0);
                        keep = t * t * (3.0 - 2.0 * t);
                    }
                }
                m = static_cast<uint8_t>(
                    std::clamp(keep * 255.0 + 0.5, 0.0, 255.0));
            }
            XER::Bridge::Skia::ApplyMask(&px, w, h, mask);
            SkiaToFrame(px, frame);
            return;
        }
    }

    // CPU fallback — Euclidean RGB distance + smoothstep window
    // (original compositor kernel, preserved for no-Skia builds).
    std::uint8_t* d = frame.Data();
    for (int i = 0; i < count; ++i) {
        std::uint8_t* p = d + static_cast<std::size_t>(i) * 4u;
        if (p[3] == 0) {
            continue;   // already transparent — nothing to key
        }
        const double dr = static_cast<double>(p[2]) - key_r;
        const double dg = static_cast<double>(p[1]) - key_g;
        const double db = static_cast<double>(p[0]) - key_b;
        const double dist = std::sqrt(dr * dr + dg * dg + db * db);
        double keep;
        if (hi <= lo) {
            keep = dist > tolerance ? 1.0 : 0.0;   // hard edge
        } else {
            const double t = std::clamp((dist - lo) / (hi - lo), 0.0, 1.0);
            keep = t * t * (3.0 - 2.0 * t);   // smoothstep
        }
        p[3] = static_cast<std::uint8_t>(
            std::clamp(p[3] * keep + 0.5, 0.0, 255.0));
    }
}

void VideoEffects::ColorBalance(Frame& frame, const double lift[3],
                                const double gain[3]) {
    if (!frame.IsValid() || lift == nullptr || gain == nullptr) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        // Lift/gain maps exactly onto the channel mixer: out_c =
        // gain_c*in_c + lift_c*255. NOTE: the bridge's AdjustColorBalance
        // is a different op (tonal shadow/midtone/highlight deltas) and
        // cannot express multiplicative gain, so AdjustChannelMixer is
        // the faithful route — same per-pixel formula as the CPU path.
        auto& px = FrameToSkia(frame);
        const XER::Bridge::Skia::ChannelMixerCoeff
            out_r{gain[0], 0.0, 0.0, lift[0]},
            out_g{0.0, gain[1], 0.0, lift[1]},
            out_b{0.0, 0.0, gain[2], lift[2]};
        if (XER::Bridge::Skia::AdjustChannelMixer(
                &px, frame.Width(), frame.Height(), out_r, out_g, out_b)) {
            SkiaToFrame(px, frame);
            return;
        }
    }
    // CPU fallback — identical formula, BGRA byte order (channel 2 = R).
    const int count = frame.Width() * frame.Height();
    std::uint8_t* d = frame.Data();
    for (int i = 0; i < count; ++i) {
        std::uint8_t* p = d + static_cast<std::size_t>(i) * 4u;
        for (int c = 0; c < 3; ++c) {
            const int rgb = 2 - c;   // pixel channel for {r,g,b}[c]
            p[rgb] = static_cast<std::uint8_t>(std::clamp(
                p[rgb] * gain[c] + lift[c] * 255.0 + 0.5, 0.0, 255.0));
        }
    }
}

void VideoEffects::Posterize(Frame& frame, int levels) {
    if (!frame.IsValid()) return;
    if (levels >= 256) {
        return;   // identity — matches the CPU math exactly
    }
    levels = std::max(2, levels);
    if (XER::Bridge::Skia::IsAvailable()) {
        auto& px = FrameToSkia(frame);
        if (XER::Bridge::Skia::AdjustPosterize(&px, frame.Width(),
                                               frame.Height(), levels)) {
            SkiaToFrame(px, frame);
            return;
        }
    }
    // CPU fallback — out = round(in*(L-1)/255) * 255/(L-1) per channel.
    const int count = frame.Width() * frame.Height();
    std::uint8_t* d = frame.Data();
    const double steps = static_cast<double>(levels - 1);
    for (int i = 0; i < count; ++i) {
        std::uint8_t* p = d + static_cast<std::size_t>(i) * 4u;
        for (int c = 0; c < 3; ++c) {
            p[c] = static_cast<std::uint8_t>(std::clamp(
                std::lround(p[c] * steps / 255.0) * (255.0 / steps) + 0.5,
                0.0, 255.0));
        }
    }
}

void VideoEffects::GrayscaleWithMask(Frame& frame, const MaskPointsVector& mask) {
    if (!frame.IsValid() || mask.Empty()) return;
    int w = frame.Width(), h = frame.Height();
    uint8_t* px = frame.Data();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!mask.Contains(static_cast<float>(x), static_cast<float>(y))) continue;
            int idx = (y*w+x)*4;
            // BGRA: luma from R,G,B
            uint8_t luma = static_cast<uint8_t>(
                0.299f*px[idx+2] + 0.587f*px[idx+1] + 0.114f*px[idx+0]);
            px[idx] = px[idx+1] = px[idx+2] = luma;
        }
}

}  // namespace video