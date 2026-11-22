// brush.cc — see brush.h.
#include "photo/brush.h"

#include <algorithm>
#include <cmath>

#include "photo/document.h"
#include "photo/photo_history.h"
#include "photo/selection.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Photo {

namespace {

float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Radial coverage of the tip at normalized radius `rn` (0 center,
// 1 edge): `hardness` is the fraction that is fully covered, the rest
// falls off with a smoothstep so the edge has no visible ring.
float TipCoverage(float rn, float hardness) {
    if (rn >= 1.0f) return 0.0f;
    const float h = ClampF(hardness, 0.0f, 0.99f);
    if (rn <= h) return 1.0f;
    const float t = (rn - h) / (1.0f - h);          // 0..1 across falloff
    const float s = t * t * (3.0f - 2.0f * t);       // smoothstep
    return 1.0f - s;
}

}  // namespace

BrushEngine::BrushEngine(BrushParams params)
    : params_(std::move(params)), rng_(params_.rng_seed) {
    if (params_.diameter_px < 1.0f) params_.diameter_px = 1.0f;
    params_.spacing = ClampF(params_.spacing, 0.01f, 4.0f);
    params_.flow = ClampF(params_.flow, 0.0f, 1.0f);
    params_.opacity = ClampF(params_.opacity, 0.0f, 1.0f);
}

const std::vector<uint32_t>& BrushEngine::TipForSize(int d) {
    auto& cache = params_.eraser ? eraser_tip_cache_ : tip_cache_;
    const auto it = cache.find(d);
    if (it != cache.end()) return it->second;

    std::vector<uint32_t> tip(static_cast<size_t>(d) * d);
    const float radius = static_cast<float>(d) * 0.5f;
    const float center = radius - 0.5f;  // pixel-center convention
    for (int y = 0; y < d; ++y) {
        for (int x = 0; x < d; ++x) {
            const float dx = x - center;
            const float dy = y - center;
            const float rn = std::sqrt(dx * dx + dy * dy) / radius;
            const float cov =
                TipCoverage(rn, params_.hardness);
            uint8_t a;
            uint8_t r = params_.color.r;
            uint8_t g = params_.color.g;
            uint8_t b = params_.color.b;
            if (params_.eraser) {
                // dstOut consumes src alpha only; paint alpha ramp
                // with white rgb.
                a = static_cast<uint8_t>(
                    ClampF(cov * 255.0f, 0.0f, 255.0f));
                r = g = b = 255;
            } else {
                a = static_cast<uint8_t>(ClampF(
                    cov * static_cast<float>(params_.color.a), 0.0f,
                    255.0f));
            }
            tip[static_cast<size_t>(y) * d + x] =
                Pixel{r, g, b, a}.PackedU32();
        }
    }
    return cache.emplace(d, std::move(tip)).first->second;
}

void BrushEngine::Stamp(float cx, float cy, float pressure) {
    if (scratch_ == nullptr) return;
    std::uniform_real_distribution<float> uni(-1.0f, 1.0f);
    const float size_j =
        1.0f + uni(rng_) * ClampF(params_.jitter_size, 0.0f, 1.0f);
    const float pos_j = ClampF(params_.jitter_pos, 0.0f, 1.0f) *
                        params_.diameter_px;
    const float jx = uni(rng_) * pos_j;
    const float jy = uni(rng_) * pos_j;

    // Pressure scales the dab (10% floor keeps pressure 0 visible).
    const float dia_f = params_.diameter_px * size_j *
                        (0.1f + 0.9f * ClampF(pressure, 0.0f, 1.0f));
    const int d = std::max(1, static_cast<int>(std::lround(dia_f)));
    const std::vector<uint32_t>& tip = TipForSize(d);

    const int dx = static_cast<int>(std::lround(cx + jx)) - d / 2;
    const int dy = static_cast<int>(std::lround(cy + jy)) - d / 2;
    const float alpha = params_.flow * ClampF(pressure, 0.0f, 1.0f);
    scratch_->CompositeRect(scratch_->Bounds(), tip, d, d, dx, dy,
                            "srcOver", alpha);
    dirty_ = IntRect::Union(dirty_, IntRect{dx, dy, d, d});
}

void BrushEngine::BeginStroke(TiledDevice& scratch) {
    scratch_ = &scratch;
    have_last_ = false;
    dist_since_dab_ = 0.0f;
    dirty_ = {};
}

IntRect BrushEngine::LineTo(const StrokePoint& p) {
    if (scratch_ == nullptr) return dirty_;
    if (!have_last_) {
        Stamp(p.x, p.y, p.pressure);
        last_ = p;
        have_last_ = true;
        dist_since_dab_ = 0.0f;
        return dirty_;
    }
    const float dx = p.x - last_.x;
    const float dy = p.y - last_.y;
    const float seg = std::sqrt(dx * dx + dy * dy);
    const float spacing_px =
        std::max(1.0f, params_.diameter_px * params_.spacing);
    if (seg <= 0.0f) {
        last_ = p;
        return dirty_;
    }
    const float ux = dx / seg;
    const float uy = dy / seg;
    float travelled = 0.0f;
    float need = spacing_px - dist_since_dab_;
    while (travelled + need <= seg + 1e-4f) {
        travelled += need;
        const float t = travelled / seg;
        const float pressure =
            last_.pressure + (p.pressure - last_.pressure) * t;
        Stamp(last_.x + ux * travelled, last_.y + uy * travelled,
              pressure);
        dist_since_dab_ = 0.0f;
        need = spacing_px;
    }
    dist_since_dab_ += seg - travelled;
    last_ = p;
    return dirty_;
}

IntRect BrushEngine::EndStroke() {
    const IntRect out =
        IntRect::Intersect(dirty_, scratch_ ? scratch_->Bounds()
                                            : IntRect{});
    scratch_ = nullptr;
    have_last_ = false;
    return out;
}

IntRect BrushEngine::PaintStroke(
    TiledDevice& scratch, const std::vector<StrokePoint>& points) {
    BeginStroke(scratch);
    for (const StrokePoint& p : points) LineTo(p);
    return EndStroke();
}

bool BrushEngine::CompositeStroke(Document& doc, LayerId layer,
                                  const TiledDevice& scratch,
                                  const IntRect& dirty,
                                  PhotoHistory* hist,
                                  const TiledDevice* clip) const {
    LayerNode* node = doc.FindLayer(layer);
    if (node == nullptr || node->Locked()) return false;
    TiledDevice* dev = node->Device();
    if (dev == nullptr || dirty.IsEmpty()) return false;
    std::vector<uint32_t> src = scratch.ReadRegionU32(dirty);
    if (clip != nullptr) {
        // Selection clip: dab alpha scaled by coverage — coverage 0
        // makes the dab pixel invisible to the composite.
        ScaleAlphaByCoverage(&src, dirty.w, dirty.h, dirty.x, dirty.y,
                             *clip);
    }
    const std::string blend =
        params_.eraser ? "dstOut" : params_.blend_mode;
    const float opacity = params_.opacity;
    const auto op = [&](TiledDevice& d) {
        d.CompositeRect(dirty, src, dirty.w, dirty.h, dirty.x, dirty.y,
                        blend, opacity);
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(
            layer, params_.eraser ? "Erase" : "Brush Stroke", op);
    }
    op(*dev);
    return true;
}

bool BrushEngine::CompositeStrokeOnMask(
    TiledDevice& mask_dev, const TiledDevice& scratch,
    const IntRect& dirty, uint8_t value, PhotoHistory* hist,
    const TiledDevice* clip) const {
    if (dirty.IsEmpty()) return false;
    std::vector<uint32_t> src = scratch.ReadRegionU32(dirty);
    if (clip != nullptr) {
        ScaleAlphaByCoverage(&src, dirty.w, dirty.h, dirty.x, dirty.y,
                             *clip);
    }
    const float opacity = params_.opacity;
    const auto op = [&](TiledDevice& d) {
        for (int y = 0; y < dirty.h; ++y) {
            for (int x = 0; x < dirty.w; ++x) {
                const uint32_t v =
                    src[static_cast<size_t>(y) * dirty.w + x];
                // Dab alpha x stroke opacity = how hard coverage is
                // pushed toward `value`.
                const int sa = static_cast<int>(
                    (v & 0xFFu) * opacity + 0.5f);
                if (sa <= 0) continue;
                const uint8_t cov =
                    d.ReadCoverage(dirty.x + x, dirty.y + y);
                const int out =
                    (cov * (255 - sa) + value * sa + 127) / 255;
                if (out != cov) {
                    d.WriteCoverage(dirty.x + x, dirty.y + y,
                                    static_cast<uint8_t>(out));
                }
            }
        }
    };
    if (hist != nullptr) {
        return hist->ExecuteDeviceOp(mask_dev, "Paint Mask", op);
    }
    op(mask_dev);
    return true;
}

bool BrushEngine::PaintStrokeOnLayer(
    Document& doc, LayerId layer, const std::vector<StrokePoint>& points,
    PhotoHistory* hist, IntRect* dirty_out,
    const TiledDevice* clip) {
    TiledDevice scratch(doc.CanvasWidth(), doc.CanvasHeight(),
                        image::PixelFormat::RGBA8, Pixel::Transparent());
    const IntRect dirty = PaintStroke(scratch, points);
    if (dirty_out) *dirty_out = dirty;
    if (dirty.IsEmpty()) return false;
    return CompositeStroke(doc, layer, scratch, dirty, hist, clip);
}

}  // namespace XER::VideoEditor::Photo
