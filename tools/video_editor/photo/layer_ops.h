// layer_ops.h — M2 layer operations: move/reparent, merge down,
// flatten image, duplicate, and pixel transforms. Each op is a
// one-call convenience in the FillLayer style: with a PhotoHistory it
// runs as an undoable PhotoCommand; with hist == nullptr it applies
// directly and leaves no undo record.
//
// Undo mechanics:
//  * Move / Merge / Flatten / Duplicate are structural commands —
//    they detach/adopt subtrees and (for merge) hold a TileMemento of
//    the merged device, so undo restores both structure and pixels.
//  * Transforms ride the tile-transaction path (ExecutePaintOp): the
//    warped full-canvas buffer is written inside one transaction and
//    the memento is the undo record — Skia's ImageCompositeWarp does
//    the resampling.
#pragma once

#include <cstddef>
#include <string>

#include "photo/document.h"
#include "photo/tile_store.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Photo {

class PhotoHistory;

// Reparent/reorder `layer` under `new_parent` (a group; nullptr =
// root) at `index` in the children vector (0 = bottom). Fails when
// the target is the layer itself or inside its subtree, when the
// parent is not a group, or the layer is the root. Undo label
// "Move Layer".
bool MoveLayer(Document& doc, LayerId layer, LayerId new_parent,
               size_t index, PhotoHistory* hist);

// Merges `layer` into the sibling directly below it in the same
// parent: the upper subtree's isolated composite lands on the lower
// raster device with the upper node's blend mode + opacity, then the
// upper node is removed. Requires the lower sibling to be a raster
// layer and both sides to be unlocked. Undo label "Merge Down".
bool MergeDown(Document& doc, LayerId layer, PhotoHistory* hist);

// Collapses the whole document to a single raster layer holding the
// flattened composite ("Flattened"). Undo restores the full tree.
// Undo label "Flatten Image".
bool FlattenImage(Document& doc, PhotoHistory* hist);

// Deep-copies `layer` (fresh ids, COW-shared tiles) and inserts the
// copy directly above the source. Returns the new layer's id, or
// kInvalidLayerId on failure. Undo label "Duplicate Layer".
LayerId DuplicateLayer(Document& doc, LayerId layer,
                       PhotoHistory* hist);

// Warps a raster layer's pixels: `matrix` maps current document
// space into the new placement (source -> destination), resampled by
// the Skia bridge (bilinear). `label` is the undo name. Refuses
// locked/group/missing layers. Falls back to a no-op-false when the
// native raster backend is unavailable.
bool TransformLayer(Document& doc, LayerId layer,
                    const XER::Bridge::Skia::Matrix3x3& matrix,
                    PhotoHistory* hist,
                    const std::string& label = "Transform Layer");

// Convenience: pure translation / uniform-anisotropic scale.
bool MoveLayerPixels(Document& doc, LayerId layer, double dx,
                     double dy, PhotoHistory* hist);
bool ScaleLayerPixels(Document& doc, LayerId layer, double sx,
                      double sy, PhotoHistory* hist);

}  // namespace XER::VideoEditor::Photo
