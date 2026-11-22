// mask_ops.h — M4 layer-mask lifecycle ops. A mask is a canvas-sized
// Gray8 (A8) TiledDevice hung off LayerNode::mask_ (default coverage
// 255 = fully visible, so a fresh mask is a no-op). Attach/detach are
// structural commands; painting into the mask rides
// PhotoHistory::ExecuteDeviceOp with the mask device pointer — see
// the lifetime note there.
#pragma once

#include "photo/document.h"

namespace XER::VideoEditor::Photo {

class PhotoHistory;

// Attaches a fresh white A8 mask to `layer` (raster/group/adjustment;
// fails on missing nodes or when a mask already exists). Undo detaches
// it into the command — redo re-attaches the SAME device object, so
// painted content survives detach->undo->redo round trips.
// Undo label "Add Layer Mask".
bool AttachLayerMask(Document& doc, LayerId layer, PhotoHistory* hist);

// Detaches the node's mask (undo restores it with pixels intact).
// Fails when the node has no mask. Undo label "Remove Layer Mask".
bool DetachLayerMask(Document& doc, LayerId layer, PhotoHistory* hist);

}  // namespace XER::VideoEditor::Photo
