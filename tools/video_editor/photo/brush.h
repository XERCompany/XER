// brush.h — M1 dab engine. Stamps a generated soft tip along a stroke
// polyline with spacing/flow/opacity/jitter/pressure, onto a SCRATCH
// TiledDevice, then composites the scratch onto the layer at stroke
// end inside one undoable tile transaction — Krita's stroke-isolation
// model (KisPainter paints a paint device; the memento lands at
// composite time). Eraser = same dabs composited with dstOut.
//
// Deliberately parameter-driven (preset-as-data) so an M2 IBrushEngine
// / libmypaint binding can sit behind the same call shape.
#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>

#include "photo/tile_store.h"

namespace XER::VideoEditor::Photo {

class Document;
class PhotoHistory;
using LayerId = uint64_t;  // same alias as document.h

struct StrokePoint {
    float x = 0.0f;
    float y = 0.0f;
    float pressure = 1.0f;  // 0..1; scales dab size and alpha
};

struct BrushParams {
    float diameter_px = 32.0f;  // tip diameter at pressure 1
    float hardness = 0.8f;      // 0 = fully soft, 1 = crisp edge
    float spacing = 0.25f;      // dab spacing, fraction of diameter
    float flow = 1.0f;          // per-dab alpha multiplier
    float opacity = 1.0f;       // stroke-level alpha at composite
    float jitter_pos = 0.0f;    // position jitter, fraction of diameter
    float jitter_size = 0.0f;   // size jitter, fraction of diameter
    uint32_t rng_seed = 0;      // deterministic when fixed (tests!)
    Pixel color = Pixel::RGBA(30, 80, 220, 255);
    bool eraser = false;        // composite dabs with dstOut
    // Skia blend-mode name for the stroke composite (eraser ignores
    // this and always uses dstOut).
    std::string blend_mode = "srcOver";
};

class BrushEngine {
public:
    explicit BrushEngine(BrushParams params);
    ~BrushEngine() = default;

    const BrushParams& Params() const { return params_; }

    // Streaming stroke: BeginStroke -> LineTo* -> EndStroke. Dabs land
    // on `scratch` immediately; the dirty rect accumulates.
    void BeginStroke(TiledDevice& scratch);
    IntRect LineTo(const StrokePoint& p);
    IntRect EndStroke();

    // One-shot polyline helper: Begin + LineTo over all points + End.
    IntRect PaintStroke(TiledDevice& scratch,
                        const std::vector<StrokePoint>& points);

    // Composite a finished scratch stroke onto `layer` inside a single
    // undoable transaction (undo label "Brush Stroke"/"Erase").
    // `clip` is an optional A8 coverage device (a selection): each dab
    // pixel's alpha is multiplied by the coverage at the same document
    // position — coverage 0 blocks paint entirely (M4).
    bool CompositeStroke(Document& doc, LayerId layer,
                         const TiledDevice& scratch, const IntRect& dirty,
                         PhotoHistory* hist,
                         const TiledDevice* clip = nullptr) const;

    // Mask-editing composite (M4): instead of blending color onto a
    // raster device, this lerps an A8 mask's coverage toward `value`
    // (0=black erase coverage, 255=white restore) by each dab pixel's
    // alpha — the Photoshop "paint gray into the mask" model.
    // Undoable through PhotoHistory::ExecuteDeviceOp (label
    // "Paint Mask"); `clip` clips mask painting to the selection too.
    bool CompositeStrokeOnMask(TiledDevice& mask_dev,
                               const TiledDevice& scratch,
                               const IntRect& dirty, uint8_t value,
                               PhotoHistory* hist,
                               const TiledDevice* clip = nullptr) const;

    // Convenience: scratch -> paint -> composite -> record. Returns the
    // dirty rect via `dirty_out`.
    bool PaintStrokeOnLayer(Document& doc, LayerId layer,
                            const std::vector<StrokePoint>& points,
                            PhotoHistory* hist, IntRect* dirty_out = nullptr,
                            const TiledDevice* clip = nullptr);

private:
    // Rasterizes the tip for integer diameter `d` (cached per size).
    const std::vector<uint32_t>& TipForSize(int d);
    void Stamp(float cx, float cy, float pressure);

    BrushParams params_;
    std::mt19937 rng_;
    TiledDevice* scratch_ = nullptr;
    StrokePoint last_{};
    bool have_last_ = false;
    float dist_since_dab_ = 0.0f;
    IntRect dirty_;
    std::unordered_map<int, std::vector<uint32_t>> tip_cache_;
    std::unordered_map<int, std::vector<uint32_t>> eraser_tip_cache_;
};

}  // namespace XER::VideoEditor::Photo
