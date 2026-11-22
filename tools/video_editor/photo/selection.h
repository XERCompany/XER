// selection.h — M4 selections. A selection is an A8 coverage
// TiledDevice (Gray8, canvas-sized, default 0) owned by the CALLER —
// the photo workspace keeps one per document, deliberately outside
// the layer tree so marquees never touch raster pixels until a paint
// op consumes them. 0 = unselected, 255 = fully selected; feathered
// edges keep intermediate coverage and clip paint ops proportionally.
//
// All mutators below write through the coverage API, so wrapping them
// in PhotoHistory::ExecuteDeviceOp makes every marquee/invert/feather
// undoable through the same tile-memento machinery as strokes.
//
// Rasterization reuses the Object Lab mask kernels
// (lab/object_ops.h PolygonMask/MaskToPolygon,
// lab/mask_refine.h FeatherMaskInPlace) and the Skia bridge
// MaskFromColorRange for the magic wand.
#pragma once

#include <cstdint>
#include <vector>

#include "photo/tile_store.h"

namespace XER::VideoEditor::Photo {

// How a new shape merges into existing coverage (Photoshop's marquee
// mode buttons).
enum class SelectionCombine : uint8_t {
    kReplace,    // out = shape
    kAdd,        // out = max(old, shape)
    kSubtract,   // out = old * (255-shape)/255
    kIntersect,  // out = old * shape/255
};

// Whole-device coverage ops.
void SelectNone(TiledDevice& sel);   // clear everything (default 0)
void SelectAll(TiledDevice& sel);    // coverage 255 over Bounds()
void InvertSelection(TiledDevice& sel);  // v = 255 - v over Bounds()

// Rect marquee — hard-edged 255 inside `r` (clipped to the device).
void SelectRect(TiledDevice& sel, const IntRect& r,
                SelectionCombine mode);

// Ellipse marquee inscribed in `r` (pixel-center distance test).
void SelectEllipse(TiledDevice& sel, const IntRect& r,
                   SelectionCombine mode);

// Polygon lasso via Lab::PolygonMask — even-odd fill, linear feather
// of feather_px centered on the edge (<=0 = hard edge). Needs >=3
// points; fewer clears coverage under kReplace and is a no-op
// otherwise.
void SelectPolygon(TiledDevice& sel, const std::vector<IntPoint>& pts,
                   float feather_px, SelectionCombine mode);

// Magic wand: coverage from the color distance to `pixels`'s value at
// (seed_x,seed_y) — Lab-style graded mask via
// XER::Bridge::Skia::MaskFromColorRange (255 at the seed color,
// ramping to 0 at `tolerance`). `pixels` is a w x h bridge-packed
// buffer at document origin (the flattened composite or a layer's
// region — the caller decides what the wand samples).
bool SelectColorRange(TiledDevice& sel,
                      const std::vector<uint32_t>& pixels, int w, int h,
                      int seed_x, int seed_y, int tolerance,
                      SelectionCombine mode);

// Gaussian-ish edge softening over the whole selection
// (Lab::FeatherMaskInPlace). radius_px <= ~0 is a no-op.
void FeatherSelection(TiledDevice& sel, float radius_px);

// True when every pixel has coverage 0. SelectionExtent is the
// bounding rect of nonzero coverage (empty when nothing selected).
bool SelectionIsEmpty(const TiledDevice& sel);
IntRect SelectionExtent(const TiledDevice& sel);

// Marching-ants polygon: the outer contour of coverage >= 128 traced
// by Lab::MaskToPolygon (document pixels, simplified by
// tolerance_px). Empty on an empty selection — callers fall back to
// the in-progress marquee shape for display.
std::vector<IntPoint> SelectionOutline(const TiledDevice& sel,
                                         float tolerance_px = 1.5f);

// --- clip helpers for paint ops ---------------------------------------

// Multiplies the alpha byte of px[i] (buffer w x h at document origin
// (ox,oy)) by clip coverage — how strokes/fills get clipped to a
// selection before compositing.
void ScaleAlphaByCoverage(std::vector<uint32_t>* px, int w, int h,
                          int ox, int oy, const TiledDevice& clip);

}  // namespace XER::VideoEditor::Photo
