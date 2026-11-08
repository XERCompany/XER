#include "video_effects.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <algorithm>
#include <vector>
#include <cstring>

namespace video {

// BGRA8 ↔ Skia uint32 (MakeColorRGBA(R,G,B,A) = 0xRRGGBBAA)
static std::vector<uint32_t> FrameToSkia(const Frame& f) {
    int n = f.Width() * f.Height();
    std::vector<uint32_t> px(n);
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
        auto px = FrameToSkia(frame);
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
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterBlur(&px, frame.Width(), frame.Height(),
                                          radius, radius);
        SkiaToFrame(px, frame);
    } else {
        // CPU box blur (horizontal pass only for speed)
        int w = frame.Width(), h = frame.Height();
        std::vector<uint8_t> tmp(w * h * 4);
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
                tmp[idx]=b/cnt; tmp[idx+1]=g/cnt; tmp[idx+2]=r/cnt; tmp[idx+3]=a/cnt;
            }
        }
        std::memcpy(frame.Data(), tmp.data(), w*h*4);
    }
}

void VideoEffects::GaussianBlur(Frame& frame, double sigma) {
    if (!frame.IsValid() || sigma <= 0.0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
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
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterSharpen(&px, frame.Width(), frame.Height(),
                                             amount, 1.0, 0);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::EdgeDetect(Frame& frame) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterEdgeDetect(&px, frame.Width(), frame.Height());
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::Emboss(Frame& frame) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterEmboss(&px, frame.Width(), frame.Height(), 315.0, 1.5);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::Vignette(Frame& frame, double strength, double radius) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterVignette(&px, frame.Width(), frame.Height(),
                                              strength, radius);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::ChromaticAberration(Frame& frame, double offset) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
        int off = static_cast<int>(offset);
        XER::Bridge::Skia::FilterChromaticAberration(&px, frame.Width(), frame.Height(),
                                                         off, 0, -off, 0);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::Pixelate(Frame& frame, int block_size) {
    if (!frame.IsValid() || block_size <= 0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterPixelate(&px, frame.Width(), frame.Height(), block_size);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::OilPaint(Frame& frame, int radius) {
    if (!frame.IsValid() || radius <= 0) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
        XER::Bridge::Skia::FilterOilPaint(&px, frame.Width(), frame.Height(), radius, 20);
        SkiaToFrame(px, frame);
    }
}

void VideoEffects::MotionBlur(Frame& frame, double angle, int distance) {
    if (!frame.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(frame);
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
    std::vector<uint8_t> row(w * 4);
    for (int y = 0; y < h/2; ++y) {
        int a=y*w*4, b=(h-1-y)*w*4;
        std::memcpy(row.data(), px+a, w*4);
        std::memcpy(px+a,       px+b, w*4);
        std::memcpy(px+b,  row.data(), w*4);
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