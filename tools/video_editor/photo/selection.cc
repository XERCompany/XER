// selection.cc — see selection.h.
#include "photo/selection.h"

#include <algorithm>
#include <cmath>

#include "lab/mask_refine.h"
#include "lab/object_ops.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Photo {

namespace {

Lab::IntPoint ToLab(const IntPoint& p) { return {p.x, p.y}; }

// Merges `shape` (rect.w*rect.h coverage bytes at document position
// `rect`) into `sel` under `mode`. Only `rect` is written — for
// kIntersect/kReplace the caller must have included the whole prior
// extent in `rect` so outside-shape pixels are zeroed correctly.
void CombineRegion(TiledDevice& sel, const IntRect& rect,
                   const uint8_t* shape, SelectionCombine mode) {
    for (int y = rect.y; y < rect.Bottom(); ++y) {
        const int sy = y - rect.y;
        for (int x = rect.x; x < rect.Right(); ++x) {
            const uint8_t s = shape[static_cast<size_t>(sy) * rect.w +
                                    (x - rect.x)];
            const uint8_t old = sel.ReadCoverage(x, y);
            int out = old;
            switch (mode) {
                case SelectionCombine::kReplace:
                    out = s;
                    break;
                case SelectionCombine::kAdd:
                    out = old > s ? old : s;
                    break;
                case SelectionCombine::kSubtract:
                    out = old * (255 - s) / 255;
                    break;
                case SelectionCombine::kIntersect:
                    out = old * s / 255;
                    break;
            }
            if (out != old) {
                sel.WriteCoverage(x, y, static_cast<uint8_t>(out));
            }
        }
    }
}

}  // namespace

void SelectNone(TiledDevice& sel) {
    sel.FillCoverage(sel.Bounds(), 0);
}

void SelectAll(TiledDevice& sel) {
    sel.FillCoverage(sel.Bounds(), 255);
}

void InvertSelection(TiledDevice& sel) {
    const IntRect b = sel.Bounds();
    for (int y = b.y; y < b.Bottom(); ++y) {
        for (int x = b.x; x < b.Right(); ++x) {
            const uint8_t v = sel.ReadCoverage(x, y);
            sel.WriteCoverage(x, y, static_cast<uint8_t>(255 - v));
        }
    }
}

void SelectRect(TiledDevice& sel, const IntRect& r,
                SelectionCombine mode) {
    const IntRect shape_r = IntRect::Intersect(r, sel.Bounds());
    // For kIntersect/kReplace, pixels of the OLD extent outside the
    // shape must be zeroed — combine over the union.
    IntRect area = shape_r;
    if (mode == SelectionCombine::kIntersect ||
        mode == SelectionCombine::kReplace) {
        area = IntRect::Union(area, sel.Extent());
    }
    area = IntRect::Intersect(area, sel.Bounds());
    if (area.IsEmpty()) return;
    std::vector<uint8_t> shape(static_cast<size_t>(area.w) * area.h,
                               0);
    for (int y = shape_r.y; y < shape_r.Bottom(); ++y) {
        std::fill_n(shape.data() +
                        static_cast<size_t>(y - area.y) * area.w +
                        (shape_r.x - area.x),
                    shape_r.w, static_cast<uint8_t>(255));
    }
    CombineRegion(sel, area, shape.data(), mode);
}

void SelectEllipse(TiledDevice& sel, const IntRect& r,
                   SelectionCombine mode) {
    const IntRect shape_r = IntRect::Intersect(r, sel.Bounds());
    IntRect area = shape_r;
    if (mode == SelectionCombine::kIntersect ||
        mode == SelectionCombine::kReplace) {
        area = IntRect::Union(area, sel.Extent());
    }
    area = IntRect::Intersect(area, sel.Bounds());
    if (area.IsEmpty()) return;
    std::vector<uint8_t> shape(static_cast<size_t>(area.w) * area.h,
                               0);
    if (!shape_r.IsEmpty()) {
        const double cx = r.x + r.w * 0.5;
        const double cy = r.y + r.h * 0.5;
        const double rx = std::max(0.5, r.w * 0.5);
        const double ry = std::max(0.5, r.h * 0.5);
        for (int y = shape_r.y; y < shape_r.Bottom(); ++y) {
            const double dy = (y + 0.5 - cy) / ry;
            for (int x = shape_r.x; x < shape_r.Right(); ++x) {
                const double dx = (x + 0.5 - cx) / rx;
                if (dx * dx + dy * dy <= 1.0) {
                    shape[static_cast<size_t>(y - area.y) * area.w +
                          (x - area.x)] = 255;
                }
            }
        }
    }
    CombineRegion(sel, area, shape.data(), mode);
}

void SelectPolygon(TiledDevice& sel,
                   const std::vector<IntPoint>& pts,
                   float feather_px, SelectionCombine mode) {
    if (pts.size() < 3) {
        if (mode == SelectionCombine::kReplace) SelectNone(sel);
        return;
    }
    std::vector<Lab::IntPoint> lp;
    lp.reserve(pts.size());
    for (const IntPoint& p : pts) lp.push_back(ToLab(p));
    const int w = sel.Width();
    const int h = sel.Height();
    const std::vector<uint8_t> shape = Lab::PolygonMask(
        w, h, lp, static_cast<int>(std::lround(feather_px)));
    if (shape.size() != static_cast<size_t>(w) * h) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t s = shape[static_cast<size_t>(y) * w + x];
            const uint8_t old = sel.ReadCoverage(x, y);
            int out = old;
            switch (mode) {
                case SelectionCombine::kReplace: out = s; break;
                case SelectionCombine::kAdd:
                    out = old > s ? old : s;
                    break;
                case SelectionCombine::kSubtract:
                    out = old * (255 - s) / 255;
                    break;
                case SelectionCombine::kIntersect:
                    out = old * s / 255;
                    break;
            }
            if (out != old) {
                sel.WriteCoverage(x, y, static_cast<uint8_t>(out));
            }
        }
    }
}

bool SelectColorRange(TiledDevice& sel,
                      const std::vector<uint32_t>& pixels, int w, int h,
                      int seed_x, int seed_y, int tolerance,
                      SelectionCombine mode) {
    if (seed_x < 0 || seed_y < 0 || seed_x >= w || seed_y >= h ||
        pixels.size() < static_cast<size_t>(w) * h) {
        return false;
    }
    const uint32_t seed =
        pixels[static_cast<size_t>(seed_y) * w + seed_x];
    std::vector<uint8_t> shape;
    if (!XER::Bridge::Skia::MaskFromColorRange(pixels, w, h, seed,
                                               tolerance, &shape) ||
        shape.size() != static_cast<size_t>(w) * h) {
        return false;
    }
    const IntRect r =
        IntRect::Intersect(IntRect{0, 0, w, h}, sel.Bounds());
    for (int y = r.y; y < r.Bottom(); ++y) {
        for (int x = r.x; x < r.Right(); ++x) {
            const uint8_t s = shape[static_cast<size_t>(y) * w + x];
            const uint8_t old = sel.ReadCoverage(x, y);
            int out = old;
            switch (mode) {
                case SelectionCombine::kReplace: out = s; break;
                case SelectionCombine::kAdd:
                    out = old > s ? old : s;
                    break;
                case SelectionCombine::kSubtract:
                    out = old * (255 - s) / 255;
                    break;
                case SelectionCombine::kIntersect:
                    out = old * s / 255;
                    break;
            }
            if (out != old) {
                sel.WriteCoverage(x, y, static_cast<uint8_t>(out));
            }
        }
    }
    return true;
}

void FeatherSelection(TiledDevice& sel, float radius_px) {
    if (radius_px <= 0.0f) return;
    const IntRect b = sel.Bounds();
    if (b.IsEmpty()) return;
    std::vector<uint8_t> cov = sel.ReadRegionCoverage(b);
    Lab::FeatherMaskInPlace(cov, b.w, b.h, radius_px);
    sel.WriteRegionCoverage(b, cov.data());
}

bool SelectionIsEmpty(const TiledDevice& sel) {
    const IntRect ext = sel.Extent();
    if (ext.IsEmpty()) {
        // No tiles: uniform default — empty iff default coverage is 0.
        return sel.DefaultPixel().a == 0;
    }
    const std::vector<uint8_t> cov = sel.ReadRegionCoverage(ext);
    for (const uint8_t v : cov) {
        if (v != 0) return false;
    }
    // Tiles exist but hold no coverage; holes still read default.
    return sel.DefaultPixel().a == 0;
}

IntRect SelectionExtent(const TiledDevice& sel) {
    const IntRect ext = sel.Extent();
    if (ext.IsEmpty()) {
        return sel.DefaultPixel().a > 0 ? sel.Bounds() : IntRect{};
    }
    const std::vector<uint8_t> cov = sel.ReadRegionCoverage(ext);
    int x0 = ext.w, y0 = ext.h, x1 = -1, y1 = -1;
    for (int y = 0; y < ext.h; ++y) {
        for (int x = 0; x < ext.w; ++x) {
            if (cov[static_cast<size_t>(y) * ext.w + x] == 0) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    if (x1 < 0) {
        return sel.DefaultPixel().a > 0 ? sel.Bounds() : IntRect{};
    }
    IntRect out{ext.x + x0, ext.y + y0, x1 - x0 + 1, y1 - y0 + 1};
    // Coverage could also live in holes when default > 0 — the full
    // bounds is the honest answer in that corner case.
    if (sel.DefaultPixel().a > 0) out = sel.Bounds();
    return out;
}

std::vector<IntPoint> SelectionOutline(const TiledDevice& sel,
                                         float tolerance_px) {
    const IntRect b = sel.Bounds();
    if (b.IsEmpty()) return {};
    const std::vector<uint8_t> cov = sel.ReadRegionCoverage(b);
    const std::vector<Lab::IntPoint> pts =
        Lab::MaskToPolygon(cov, b.w, b.h, tolerance_px);
    std::vector<IntPoint> out;
    out.reserve(pts.size());
    for (const Lab::IntPoint& p : pts) out.push_back({p.x, p.y});
    return out;
}

void ScaleAlphaByCoverage(std::vector<uint32_t>* px, int w, int h,
                          int ox, int oy, const TiledDevice& clip) {
    if (px == nullptr) return;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t cov = clip.ReadCoverage(ox + x, oy + y);
            if (cov < 255) {
                (*px)[static_cast<size_t>(y) * w + x] = ScaleU32Alpha(
                    (*px)[static_cast<size_t>(y) * w + x], cov);
            }
        }
    }
}

}  // namespace XER::VideoEditor::Photo
