#include "canvas.h"
#include "wrapper/skia/skia_engine_bridge.h"
#include "flux/core/logger.h"
#include <cstring>
#include <algorithm>

namespace video {

// ── Skia pixel conversion helpers ───────────────────────────────────────────
// Frame stores BGRA8: [B,G,R,A]. Skia bridge uses MakeColorRGBA(r,g,b,a).
// We pack as 0xRRGGBBAA: R<<24 | G<<16 | B<<8 | A.

static std::vector<uint32_t> FrameToSkia(const Frame& f) {
    int n = f.Width() * f.Height();
    std::vector<uint32_t> px(n);
    const uint8_t* d = f.Data();
    for (int i = 0; i < n; ++i) {
        px[i] = XER::Bridge::Skia::MakeColorRGBA(
                    d[i*4+2], d[i*4+1], d[i*4+0], d[i*4+3]);
    }
    return px;
}

static void SkiaToFrame(const std::vector<uint32_t>& px, Frame& f) {
    int n = f.Width() * f.Height();
    uint8_t* d = f.Data();
    for (int i = 0; i < n; ++i) {
        uint32_t c = px[i];
        d[i*4+2] = (c >> 24) & 0xFF;  // R
        d[i*4+1] = (c >> 16) & 0xFF;  // G
        d[i*4+0] = (c >>  8) & 0xFF;  // B
        d[i*4+3] =  c        & 0xFF;  // A
    }
}

// ─────────────────────────────────────────────────────────────────────────────

Canvas::Canvas(int width, int height, PixelFormat fmt) {
    Resize(width, height, fmt);
}

void Canvas::Resize(int width, int height, PixelFormat fmt) {
    target_.Resize(width, height, fmt);
}

void Canvas::Clear(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Frame* f = target_.GetFrame();
    if (!f || !f->IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto px = FrameToSkia(*f);
        uint32_t color = XER::Bridge::Skia::MakeColorRGBA(r, g, b, a);
        XER::Bridge::Skia::RasterClear(&px, f->Width(), f->Height(), color);
        SkiaToFrame(px, *f);
    } else {
        int n = f->Width() * f->Height() * 4;
        uint8_t* px = f->Data();
        for (int i = 0; i < n; i += 4) {
            px[i]=b; px[i+1]=g; px[i+2]=r; px[i+3]=a;  // BGRA
        }
    }
}

void Canvas::Blit(const Frame& src, int dx, int dy) {
    Frame* dst = target_.GetFrame();
    if (!dst || !dst->IsValid() || !src.IsValid()) return;
    if (XER::Bridge::Skia::IsAvailable()) {
        auto dst_px = FrameToSkia(*dst);
        auto src_px = FrameToSkia(src);
        XER::Bridge::Skia::RasterDrawImage(
                &dst_px, dst->Width(), dst->Height(),
                src_px,  src.Width(),  src.Height(),
                dx, dy, "src_over");
        SkiaToFrame(dst_px, *dst);
    } else {
        for (int y = 0; y < src.Height(); ++y) {
            int oy = dy + y;
            if (oy < 0 || oy >= dst->Height()) continue;
            for (int x = 0; x < src.Width(); ++x) {
                int ox = dx + x;
                if (ox < 0 || ox >= dst->Width()) continue;
                int si = (y * src.Width() + x) * 4;
                int di = (oy * dst->Width() + ox) * 4;
                std::memcpy(dst->Data() + di, src.Data() + si, 4);
            }
        }
    }
}

void Canvas::ApplyMask(const std::string& name, MaskAction action) {
    Frame* f = target_.GetFrame();
    if (!mask_system_ || !f) return;
    mask_system_->ApplyMask(*f, name, action);
}

std::unique_ptr<Frame> Canvas::CutFragment(const MaskPointsVector& mask, bool mask_pixels) {
    Frame* f = target_.GetFrame();
    if (!f) return nullptr;
    return cutter_.Cut(*f, mask, mask_pixels);
}

}  // namespace video