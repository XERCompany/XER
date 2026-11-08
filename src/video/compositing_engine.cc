#include "compositing_engine.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <algorithm>
#include <cstring>

namespace video {

void CompositingEngine::AddLayer(std::shared_ptr<Frame> frame, int z,
                                   int x, int y, float opacity) {
    layers_.push_back({std::move(frame), z, x, y, opacity});
}

void CompositingEngine::ClearLayers() { layers_.clear(); }

// ── pixel helpers ───────────────────────────────────────────────────────────
// BGRA8: d[0]=B, d[1]=G, d[2]=R, d[3]=A  ↔  skia uint32 MakeColorRGBA(R,G,B,A)
static inline uint32_t bgra_to_skia(const uint8_t* p) {
    return XER::Bridge::Skia::MakeColorRGBA(p[2], p[1], p[0], p[3]);
}
static inline void skia_to_bgra(uint32_t c, uint8_t* p) {
    p[2] = (c >> 24) & 0xFF;  // R
    p[1] = (c >> 16) & 0xFF;  // G
    p[0] = (c >>  8) & 0xFF;  // B
    p[3] =  c        & 0xFF;  // A
}

bool CompositingEngine::Composite(Frame& output) {
    if (!output.IsValid()) return false;
    std::sort(layers_.begin(), layers_.end(),
              [](const Layer& a, const Layer& b){ return a.z_order < b.z_order; });

    bool use_skia = XER::Bridge::Skia::IsAvailable();

    // Convert output to Skia pixel buffer
    int ow = output.Width(), oh = output.Height();
    std::vector<uint32_t> dst_px;
    if (use_skia) {
        dst_px.resize(ow * oh);
        const uint8_t* d = output.Data();
        for (int i = 0; i < ow * oh; ++i) dst_px[i] = bgra_to_skia(d + i * 4);
    }

    for (const auto& layer : layers_) {
        if (!layer.frame || !layer.frame->IsValid()) continue;
        const Frame& src = *layer.frame;

        if (use_skia) {
            // Apply opacity by scaling alpha channel in a copy
            std::vector<uint32_t> src_px(src.Width() * src.Height());
            const uint8_t* s = src.Data();
            for (int i = 0; i < src.Width() * src.Height(); ++i) {
                uint8_t scaled_a = static_cast<uint8_t>(s[i*4+3] * layer.opacity);
                src_px[i] = XER::Bridge::Skia::MakeColorRGBA(
                                s[i*4+2], s[i*4+1], s[i*4+0], scaled_a);
            }
            XER::Bridge::Skia::RasterDrawImage(
                &dst_px, ow, oh,
                src_px,  src.Width(), src.Height(),
                layer.x_offset, layer.y_offset, "src_over");
        } else {
            for (int sy = 0; sy < src.Height(); ++sy) {
                int dy = layer.y_offset + sy;
                if (dy < 0 || dy >= oh) continue;
                for (int sx = 0; sx < src.Width(); ++sx) {
                    int dx = layer.x_offset + sx;
                    if (dx < 0 || dx >= ow) continue;
                    int si = (sy * src.Width() + sx) * 4;
                    int di = (dy * ow + dx) * 4;
                    const uint8_t* sp = src.Data() + si;
                    uint8_t*       dp = output.Data() + di;
                    float a = sp[3] / 255.0f * layer.opacity;
                    dp[0] = static_cast<uint8_t>(dp[0] * (1-a) + sp[0] * a);
                    dp[1] = static_cast<uint8_t>(dp[1] * (1-a) + sp[1] * a);
                    dp[2] = static_cast<uint8_t>(dp[2] * (1-a) + sp[2] * a);
                    dp[3] = 255;
                }
            }
        }
    }

    if (use_skia) {
        uint8_t* d = output.Data();
        for (int i = 0; i < ow * oh; ++i) skia_to_bgra(dst_px[i], d + i * 4);
    }
    return true;
}

}  // namespace video