#include "mask_processor.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include <vector>
#include <utility>
#include <cstring>
#include <algorithm>

namespace video {

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
    for (int i = 0, n=(int)px.size(); i < n; ++i) {
        uint32_t c = px[i];
        d[i*4+2]=(c>>24)&0xFF; d[i*4+1]=(c>>16)&0xFF;
        d[i*4+0]=(c>>8)&0xFF;  d[i*4+3]=c&0xFF;
    }
}

void MaskProcessor::Apply(Frame& frame, const MaskPointsVector& mask,
                           MaskAction action,
                           uint8_t fill_r, uint8_t fill_g,
                           uint8_t fill_b, uint8_t fill_a) {
    if (!frame.IsValid() || mask.Empty()) return;
    if (frame.Format() != PixelFormat::BGRA8 &&
        frame.Format() != PixelFormat::RGBA8) return;

    int w = frame.Width(), h = frame.Height();

    if (XER::Bridge::Skia::IsAvailable()) {
        // Build a greyscale mask via Skia's RasterFillPolygon
        // First, create a mask frame (all-zero = outside polygon)
        std::vector<uint32_t> mask_px(w * h, 0x000000FF);  // black opaque

        // Convert MaskPointsVector to vector<pair<int,int>>
        std::vector<std::pair<int,int>> pts;
        pts.reserve(mask.Size());
        for (const auto& mp : mask.Points())
            pts.emplace_back(static_cast<int>(mp.x), static_cast<int>(mp.y));

        uint32_t white = XER::Bridge::Skia::MakeColorRGBA(255, 255, 255, 255);
        XER::Bridge::Skia::RasterFillPolygon(&mask_px, w, h, pts, white, "src");

        // Build greyscale mask: 255 inside polygon, 0 outside
        std::vector<uint8_t> grey(w * h);
        for (int i = 0; i < w * h; ++i)
            grey[i] = ((mask_px[i] >> 24) & 0xFF);  // R channel = 255 if white

        if (action == MaskAction::CUT_OUTSIDE || action == MaskAction::FILL_OUTSIDE) {
            // Invert: we want to zero/fill pixels OUTSIDE the polygon
            // grey[i]==0 means outside → fill those

            auto frame_px = FrameToSkia(frame);
            uint32_t fill_color = XER::Bridge::Skia::MakeColorRGBA(fill_r, fill_g, fill_b, fill_a);
            for (int i = 0; i < w * h; ++i) {
                if (grey[i] == 0)
                    frame_px[i] = fill_color;
            }
            SkiaToFrame(frame_px, frame);

        } else {  // CUT_INSIDE
            auto frame_px = FrameToSkia(frame);
            uint32_t fill_color = XER::Bridge::Skia::MakeColorRGBA(fill_r, fill_g, fill_b, fill_a);
            for (int i = 0; i < w * h; ++i) {
                if (grey[i] != 0)
                    frame_px[i] = fill_color;
            }
            SkiaToFrame(frame_px, frame);
        }
    } else {
        // CPU fallback: ray-cast per pixel
        uint8_t* px = frame.Data();
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                bool inside = mask.Contains(static_cast<float>(x), static_cast<float>(y));
                bool should_fill =
                    (action == MaskAction::CUT_INSIDE   &&  inside) ||
                    (action == MaskAction::CUT_OUTSIDE   && !inside) ||
                    (action == MaskAction::FILL_OUTSIDE  && !inside);
                if (should_fill) {
                    int idx = (y * w + x) * 4;
                    px[idx+0] = fill_b;  // BGRA: B first
                    px[idx+1] = fill_g;
                    px[idx+2] = fill_r;
                    px[idx+3] = fill_a;
                }
            }
        }
    }
}

}  // namespace video
