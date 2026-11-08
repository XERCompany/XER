#include "mix_system.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <cstring>
#include <algorithm>

namespace video {

// Mode name → Skia blend mode string
static const char* SkiaBlendName(MixMode mode) {
    switch (mode) {
        case MixMode::Alpha:    return "src_over";
        case MixMode::Additive: return "plus";
        case MixMode::Multiply: return "multiply";
        case MixMode::Screen:   return "screen";
    }
    return "src_over";
}

// BGRA8 pixel i → Skia uint32 (MakeColorRGBA packs R,G,B,A → 0xRRGGBBAA)
static inline uint32_t bgra_to_skia(const uint8_t* d) {
    return XER::Bridge::Skia::MakeColorRGBA(d[2], d[1], d[0], d[3]);
}
static inline void skia_to_bgra(uint32_t c, uint8_t* d) {
    d[2] = (c >> 24) & 0xFF;  // R
    d[1] = (c >> 16) & 0xFF;  // G
    d[0] = (c >>  8) & 0xFF;  // B
    d[3] =  c        & 0xFF;  // A
}

bool MixSystem::Mix(Frame& dst, const Frame& src, MixMode mode, float weight) {
    if (!dst.IsValid() || !src.IsValid()) return false;
    if (dst.Width() != src.Width() || dst.Height() != src.Height()) return false;
    int n = dst.Width() * dst.Height();

    if (XER::Bridge::Skia::IsAvailable()) {
        // Scale source alpha by weight, then blend via Skia
        std::vector<uint32_t> dst_px(n), src_px(n);
        const uint8_t* sp = src.Data();
        const uint8_t* dp = dst.Data();
        for (int i = 0; i < n; ++i) {
            dst_px[i] = bgra_to_skia(dp + i * 4);
            uint8_t scaled_a = static_cast<uint8_t>(sp[i*4+3] * weight);
            src_px[i] = XER::Bridge::Skia::MakeColorRGBA(
                            sp[i*4+2], sp[i*4+1], sp[i*4+0], scaled_a);
        }
        // Apply blend per-pixel using BlendModeApply
        const char* blend = SkiaBlendName(mode);
        for (int i = 0; i < n; ++i)
            dst_px[i] = XER::Bridge::Skia::BlendModeApply(blend, dst_px[i], src_px[i]);
        uint8_t* out = dst.Data();
        for (int i = 0; i < n; ++i) skia_to_bgra(dst_px[i], out + i * 4);
    } else {
        // CPU fallback
        uint8_t* dp_raw = dst.Data();
        const uint8_t* sp_raw = src.Data();
        for (int i = 0; i < n; ++i) {
            int idx = i * 4;
            float a = weight * sp_raw[idx+3] / 255.0f;
            auto cu8 = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(v, 0.f, 255.f));
            };
            switch (mode) {
                case MixMode::Alpha:
                    dp_raw[idx+0]=cu8(dp_raw[idx+0]*(1-a)+sp_raw[idx+0]*a);
                    dp_raw[idx+1]=cu8(dp_raw[idx+1]*(1-a)+sp_raw[idx+1]*a);
                    dp_raw[idx+2]=cu8(dp_raw[idx+2]*(1-a)+sp_raw[idx+2]*a);
                    break;
                case MixMode::Additive:
                    dp_raw[idx+0]=cu8(dp_raw[idx+0]+sp_raw[idx+0]*weight);
                    dp_raw[idx+1]=cu8(dp_raw[idx+1]+sp_raw[idx+1]*weight);
                    dp_raw[idx+2]=cu8(dp_raw[idx+2]+sp_raw[idx+2]*weight);
                    break;
                case MixMode::Multiply:
                    dp_raw[idx+0]=cu8(dp_raw[idx+0]*sp_raw[idx+0]/255.f);
                    dp_raw[idx+1]=cu8(dp_raw[idx+1]*sp_raw[idx+1]/255.f);
                    dp_raw[idx+2]=cu8(dp_raw[idx+2]*sp_raw[idx+2]/255.f);
                    break;
                case MixMode::Screen:
                    dp_raw[idx+0]=cu8(255.f-(255.f-dp_raw[idx+0])*(255.f-sp_raw[idx+0])/255.f);
                    dp_raw[idx+1]=cu8(255.f-(255.f-dp_raw[idx+1])*(255.f-sp_raw[idx+1])/255.f);
                    dp_raw[idx+2]=cu8(255.f-(255.f-dp_raw[idx+2])*(255.f-sp_raw[idx+2])/255.f);
                    break;
            }
            dp_raw[idx+3] = 255;
        }
    }
    return true;
}

std::unique_ptr<Frame> MixSystem::Blend(const Frame& a, const Frame& b,
                                          MixMode mode, float t) {
    if (!a.IsValid() || !b.IsValid()) return nullptr;
    auto out = std::make_unique<Frame>();
    out->Allocate(a.Width(), a.Height(), a.Format());
    std::memcpy(out->Data(), a.Data(), a.Width() * a.Height() * 4);
    Mix(*out, b, mode, t);
    return out;
}

}  // namespace video