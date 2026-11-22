// retouch.h — M4 clone/heal/spot/inpaint on photo layers. Thin
// patch-bridge over image::RetouchTool (src/image/tools/): each op
// rasterizes the smallest layer region covering the tool's circles
// into an RGBA8 ImageBuffer, runs the tool, and writes the patch
// back inside one tile transaction — undoable through
// PhotoHistory::ExecutePaintOp like any stroke.
//
// `clip` is an optional A8 coverage device (a selection): written
// pixels lerp toward the tool result by coverage — 0 leaves the
// destination untouched, so retouch respects the marquee like every
// other paint op.
//
// Drags are sequences of single dabs — callers wrap a whole drag in
// PhotoHistory::BeginMacro/EndMacro for a single undo step.
#pragma once

#include "photo/document.h"

namespace XER::VideoEditor::Photo {

class PhotoHistory;

// CloneStamp: copies the radius-circle around `src` onto `dst`
// (document pixels). Undo label "Clone Stamp".
bool CloneStampAt(Document& doc, LayerId layer, IntPoint src,
                  IntPoint dst, int radius, PhotoHistory* hist,
                  const TiledDevice* clip = nullptr);

// HealingBrush: clone + 50% blend into the destination.
// Undo label "Healing Brush".
bool HealingBrushAt(Document& doc, LayerId layer, IntPoint src,
                    IntPoint dst, int radius, PhotoHistory* hist,
                    const TiledDevice* clip = nullptr);

// SpotRemove: fills the radius-circle around `at` with the average
// border-ring color. Undo label "Spot Remove".
bool SpotRemoveAt(Document& doc, LayerId layer, IntPoint at,
                  int radius, PhotoHistory* hist,
                  const TiledDevice* clip = nullptr);

// Inpaint over a hole mask (the workspace selection): diffusion fill
// where coverage >= 128, `iterations` sweeps. Undo label "Inpaint".
bool InpaintMasked(Document& doc, LayerId layer,
                   const TiledDevice& hole, PhotoHistory* hist,
                   int iterations = 50);

}  // namespace XER::VideoEditor::Photo
