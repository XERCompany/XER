// retouch.cc — see retouch.h.
#include "photo/retouch.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

#include "image/tools/retouch_tool.h"
#include "photo/photo_history.h"

namespace XER::VideoEditor::Photo {

namespace {

// Builds an RGBA8 ImageBuffer holding the layer's pixels over `rect`
// (device coords) — the RGBA quad layout matches TiledDevice's
// normalized ReadRegion, so this is a straight copy.
image::ImageBuffer PatchFromDevice(const TiledDevice& dev,
                                   const IntRect& rect) {
    image::ImageBuffer img(rect.w, rect.h, image::PixelFormat::RGBA8);
    if (!img.IsValid()) return img;
    const std::vector<uint8_t> bytes = dev.ReadRegion(rect);
    std::memcpy(img.Data(), bytes.data(),
                std::min(img.DataSize(), bytes.size()));
    return img;
}

// Writes `img` back over `rect`, restricted to the r-circle around
// `center` (device coords) and lerped by clip coverage. Runs inside
// the caller's transaction.
void WriteBackClipped(TiledDevice& dev, const image::ImageBuffer& img,
                      const IntRect& rect, IntPoint center, int radius,
                      const TiledDevice* clip) {
    const int r2 = radius * radius;
    for (int y = rect.y; y < rect.Bottom(); ++y) {
        const int dy = y - center.y;
        if (std::abs(dy) > radius) continue;
        for (int x = rect.x; x < rect.Right(); ++x) {
            const int dx = x - center.x;
            if (dx * dx + dy * dy > r2) continue;
            const uint8_t cov =
                clip != nullptr ? clip->ReadCoverage(x, y) : 255;
            if (cov == 0) continue;
            const uint8_t* s =
                img.Data() +
                (static_cast<size_t>(y - rect.y) * rect.w +
                 (x - rect.x)) * 4;
            const Pixel cur = dev.ReadPixel(x, y);
            dev.WritePixel(
                x, y,
                LerpPixelCov(cur, Pixel{s[0], s[1], s[2], s[3]}, cov));
        }
    }
}

// Shared single-dab worker: patch = union(src circle, dst circle)
// + 1px margin (SpotRemove samples a ring one pixel out), run `tool`
// on the patch, write back clipped to the dst circle.
bool Dab(Document& doc, LayerId layer, IntPoint src, IntPoint dst,
         int radius, const TiledDevice* clip,
         const char* label, PhotoHistory* hist,
         const std::function<void(image::ImageBuffer&, int, int, int,
                                  int)>& tool) {
    LayerNode* node = doc.FindLayer(layer);
    if (node == nullptr || node->Locked()) return false;
    TiledDevice* dev = node->Device();
    if (dev == nullptr || radius < 1) return false;

    IntRect dr{dst.x - radius - 1, dst.y - radius - 1,
               radius * 2 + 3, radius * 2 + 3};
    IntRect sr{src.x - radius - 1, src.y - radius - 1,
               radius * 2 + 3, radius * 2 + 3};
    const IntRect rect = IntRect::Intersect(
        IntRect::Union(dr, sr), dev->Bounds());
    if (rect.IsEmpty()) return false;

    image::ImageBuffer img = PatchFromDevice(*dev, rect);
    if (!img.IsValid()) return false;
    tool(img, src.x - rect.x, src.y - rect.y, dst.x - rect.x,
         dst.y - rect.y);

    const auto op = [&](TiledDevice& d) {
        WriteBackClipped(d, img, rect, dst, radius, clip);
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, label, op);
    }
    op(*dev);
    return true;
}

}  // namespace

bool CloneStampAt(Document& doc, LayerId layer, IntPoint src,
                  IntPoint dst, int radius, PhotoHistory* hist,
                  const TiledDevice* clip) {
    return Dab(doc, layer, src, dst, radius, clip, "Clone Stamp", hist,
               [radius](image::ImageBuffer& img, int sx, int sy, int dx,
                        int dy) {
                   image::RetouchTool::CloneStamp(img, sx, sy, dx, dy,
                                                  radius);
               });
}

bool HealingBrushAt(Document& doc, LayerId layer, IntPoint src,
                    IntPoint dst, int radius, PhotoHistory* hist,
                    const TiledDevice* clip) {
    return Dab(doc, layer, src, dst, radius, clip, "Healing Brush",
               hist,
               [radius](image::ImageBuffer& img, int sx, int sy, int dx,
                        int dy) {
                   image::RetouchTool::HealingBrush(img, sx, sy, dx,
                                                    dy, radius);
               });
}

bool SpotRemoveAt(Document& doc, LayerId layer, IntPoint at,
                  int radius, PhotoHistory* hist,
                  const TiledDevice* clip) {
    // src == dst: the tool ignores the source args.
    return Dab(doc, layer, at, at, radius, clip, "Spot Remove", hist,
               [radius](image::ImageBuffer& img, int, int, int dx,
                        int dy) {
                   image::RetouchTool::SpotRemove(img, dx, dy, radius);
               });
}

bool InpaintMasked(Document& doc, LayerId layer,
                   const TiledDevice& hole, PhotoHistory* hist,
                   int iterations) {
    LayerNode* node = doc.FindLayer(layer);
    if (node == nullptr || node->Locked()) return false;
    TiledDevice* dev = node->Device();
    if (dev == nullptr) return false;

    // Patch = hole extent + 1px margin (the diffusion reads neighbors
    // at +-1); empty hole -> nothing to do.
    IntRect ext;
    {
        const IntRect he = hole.Extent();
        if (he.IsEmpty()) return false;
        ext = IntRect::Intersect(
            IntRect{he.x - 1, he.y - 1, he.w + 2, he.h + 2},
            dev->Bounds());
    }
    if (ext.IsEmpty()) return false;

    image::ImageBuffer img = PatchFromDevice(*dev, ext);
    if (!img.IsValid()) return false;
    // The tool reads the hole flag from channel 0 of an RGBA mask —
    // pack coverage there.
    image::ImageBuffer mask(ext.w, ext.h, image::PixelFormat::RGBA8);
    if (!mask.IsValid()) return false;
    const std::vector<uint8_t> cov = hole.ReadRegionCoverage(ext);
    for (size_t i = 0; i < cov.size(); ++i) {
        mask.Data()[i * 4 + 0] = cov[i];
        mask.Data()[i * 4 + 3] = 255;
    }
    image::RetouchTool::Inpaint(img, mask, iterations);

    const auto op = [&](TiledDevice& d) {
        // Write back only the hole pixels — everything else is
        // untouched by diffusion anyway.
        for (int y = ext.y; y < ext.Bottom(); ++y) {
            for (int x = ext.x; x < ext.Right(); ++x) {
                const size_t i =
                    static_cast<size_t>(y - ext.y) * ext.w +
                    (x - ext.x);
                if (cov[i] < 128) continue;
                const uint8_t* s = img.Data() + i * 4;
                d.WritePixel(x, y, Pixel{s[0], s[1], s[2], s[3]});
            }
        }
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, "Inpaint", op);
    }
    op(*dev);
    return true;
}

}  // namespace XER::VideoEditor::Photo
