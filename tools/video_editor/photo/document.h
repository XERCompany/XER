// document.h — the photo document: canvas size, working color-space
// tag, and a layer tree whose raster leaves are TiledDevice-backed.
//
// Reuse notes (docs/wiki/reports/2026-11-22-src-api-reuse-audit.md):
//  * image::ImageBuffer is the flatten/PNG-IO interchange — NOT the
//    layer store (full-frame per layer would defeat tiling).
//  * image::LayersManager was evaluated and intentionally not reused:
//    its layers own full-frame ImageBuffers with no tiles/masks —
//    M2's layer model is TiledDevice-backed per the stack doc.
//  * Compositing/PNG codec work routes through XER::Bridge::Skia and
//    image::ImageLoader/ImageSaver (both already linked).
//
// M2 notes:
//  * Groups are ISOLATED composites (Krita semantics): a group's
//    children render into a scratch buffer, then the buffer blends
//    onto the canvas below with the group's own blend mode and
//    opacity. Group blend modes ("dstOut" erase groups, "multiply"
//    tint groups, ...) are therefore meaningful, and a child inside a
//    group can never reach the pixels below the group directly.
//  * LayerNode carries the full attribute set — name, visible,
//    opacity, blend-mode name, locked — on raster AND group nodes;
//    all of them are undoable through SetLayerAttributesCommand.
//  * Structural edits (move/reparent, merge, flatten, duplicate) go
//    through the tree-surgery API on Document, which exists for the
//    PhotoCommands in layer_ops.cc / photo_history.cc.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "image/core/color_space.h"
#include "image/core/image_buffer.h"
#include "photo/tile_store.h"
#include "wrapper/skia/skia_engine_bridge.h"

namespace XER::VideoEditor::Photo {

class PhotoHistory;
using LayerId = uint64_t;
constexpr LayerId kInvalidLayerId = 0;

enum class LayerKind : uint8_t {
    kGroup,
    kRaster,
    kAdjustment,  // M4: named op + params over the composite-of-below
    // kText / kVector — M2+ per the stack doc.
};

// One named parameter of an adjustment layer op (see
// photo/adjust_ops.h for the registry of ops + param schemas).
struct AdjustmentParam {
    std::string name;
    double value = 0.0;
    bool operator==(const AdjustmentParam& o) const {
        return name == o.name && value == o.value;
    }
    bool operator!=(const AdjustmentParam& o) const {
        return !(*this == o);
    }
};

// What an kAdjustment node evaluates: `op` names a registered op
// ("hsl", "levels", "curves", ...); `params` is the ordered
// name/value list (missing entries take the op's defaults).
struct AdjustmentSpec {
    std::string op;
    std::vector<AdjustmentParam> params;
    bool operator==(const AdjustmentSpec& o) const {
        return op == o.op && params == o.params;
    }
    bool operator!=(const AdjustmentSpec& o) const {
        return !(*this == o);
    }
};

// One node of the layer tree. Groups nest; raster layers own a
// canvas-sized TiledDevice. Children are stored bottom-to-top (last
// child paints on top), matching layer-panel order.
class LayerNode {
public:
    ~LayerNode() = default;
    LayerNode(const LayerNode&) = delete;
    LayerNode& operator=(const LayerNode&) = delete;

    LayerId Id() const { return id_; }
    LayerKind Kind() const { return kind_; }
    const std::string& Name() const { return name_; }
    void SetName(const std::string& n) { name_ = n; }
    bool Visible() const { return visible_; }
    void SetVisible(bool v) { visible_ = v; }
    bool Locked() const { return locked_; }
    void SetLocked(bool l) { locked_ = l; }
    float Opacity() const { return opacity_; }
    void SetOpacity(float o) { opacity_ = o; }
    const std::string& BlendMode() const { return blend_mode_; }
    void SetBlendMode(const std::string& m) { blend_mode_ = m; }

    // Raster leaves only (nullptr for groups/adjustments).
    TiledDevice* Device() { return device_.get(); }
    const TiledDevice* Device() const { return device_.get(); }

    // --- M4: A8 layer mask -------------------------------------------
    // Optional mask attachment (raster/group/adjustment nodes): a
    // canvas-sized Gray8 TiledDevice whose coverage scales the node's
    // effective alpha at composite time (255 = fully visible, the
    // default). nullptr = no mask.
    TiledDevice* Mask() { return mask_.get(); }
    const TiledDevice* Mask() const { return mask_.get(); }
    bool HasMask() const { return mask_ != nullptr; }
    // Adopts a caller-built device (canvas-sized Gray8 expected) or
    // detaches on nullptr. Structural edits go through the mask
    // commands in mask_ops.h so they stay undoable.
    void SetMask(std::unique_ptr<TiledDevice> mask);
    // Detaches the mask and returns it (undo/redo swap path — the
    // same device object is re-attached by SetMask, so painted
    // content survives attach->detach->attach).
    std::unique_ptr<TiledDevice> TakeMask();

    // --- M4: adjustment layer payload --------------------------------
    // Meaningful on kAdjustment nodes only.
    const AdjustmentSpec& Adjustment() const { return adjustment_; }
    void SetAdjustment(const AdjustmentSpec& spec) {
        adjustment_ = spec;
    }

    LayerNode* Parent() { return parent_; }
    const LayerNode* Parent() const { return parent_; }
    const std::vector<std::unique_ptr<LayerNode>>& Children() const {
        return children_;
    }

private:
    friend class Document;
    friend class AddLayerCommand;
    LayerNode(LayerId id, LayerKind kind, std::string name)
        : id_(id), kind_(kind), name_(std::move(name)) {}

    LayerId id_;
    LayerKind kind_;
    std::string name_;
    bool visible_ = true;
    bool locked_ = false;  // pixel ops refuse to touch a locked layer
    float opacity_ = 1.0f;
    std::string blend_mode_ = "srcOver";  // Skia blend-mode name
    std::unique_ptr<TiledDevice> device_;
    // M4: optional A8 coverage mask (Gray8 TiledDevice, canvas-sized).
    std::unique_ptr<TiledDevice> mask_;
    // M4: adjustment-layer payload (kAdjustment nodes).
    AdjustmentSpec adjustment_;
    LayerNode* parent_ = nullptr;
    std::vector<std::unique_ptr<LayerNode>> children_;
};

class Document {
public:
    Document(int canvas_w, int canvas_h,
             image::ColorSpace working = image::ColorSpace::sRGB);
    ~Document() = default;
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    int CanvasWidth() const { return width_; }
    int CanvasHeight() const { return height_; }
    IntRect CanvasRect() const { return {0, 0, width_, height_}; }
    image::ColorSpace WorkingSpace() const { return working_; }
    void SetWorkingSpace(image::ColorSpace cs) { working_ = cs; }

    LayerNode& Root() { return *root_; }
    const LayerNode& Root() const { return *root_; }

    // Creates a layer under `parent` (root when nullptr). Raster
    // layers get a transparent canvas-sized TiledDevice.
    LayerNode& AddLayer(LayerKind kind, const std::string& name,
                        LayerNode* parent = nullptr);
    LayerNode& AddRasterLayer(const std::string& name,
                              LayerNode* parent = nullptr) {
        return AddLayer(LayerKind::kRaster, name, parent);
    }
    LayerNode& AddGroup(const std::string& name,
                        LayerNode* parent = nullptr) {
        return AddLayer(LayerKind::kGroup, name, parent);
    }
    // Detaches the node (and its subtree) from the tree; used by
    // structural undo commands.
    std::unique_ptr<LayerNode> DetachLayer(LayerId id);
    // Re-adopts a detached node under `parent` (undo path).
    LayerNode& AdoptLayer(LayerNode* parent,
                          std::unique_ptr<LayerNode> node);

    // --- tree surgery for structural undo commands (M2) ------------
    // Index of `child` inside parent's children vector (bottom-to-top
    // order), -1 when not a direct child.
    int ChildIndex(const LayerNode& parent, LayerId child) const;
    // Adopts `node` under `parent` at `index` (clamped to the valid
    // range; SIZE_MAX/len appends on top). Returns the adopted node.
    LayerNode& InsertChildAt(LayerNode* parent, size_t index,
                             std::unique_ptr<LayerNode> node);
    // Deep-copies a subtree with FRESH ids; raster devices are
    // copy-on-write clones, so duplicating a painted layer is O(1)
    // until either side writes.
    std::unique_ptr<LayerNode> CloneSubtree(LayerId id);
    // Takes every child of `parent` (root children for FlattenImage's
    // undo), leaving their parent pointers untouched-on-detach
    // semantics of DetachLayer (nullptr).
    std::vector<std::unique_ptr<LayerNode>> DetachAllChildren(
        LayerNode* parent);
    // Re-adopts a vector of detached children under `parent` (in
    // order). Pair of DetachAllChildren.
    void AdoptChildren(LayerNode* parent,
                       std::vector<std::unique_ptr<LayerNode>> children);
    // True when `ancestor` is `node` itself or one of its ancestors —
    // the move/reparent legality check (no cycles).
    static bool IsDescendantOrSelf(const LayerNode& node,
                                   const LayerNode& ancestor);

    LayerNode* FindLayer(LayerId id);
    const LayerNode* FindLayer(LayerId id) const;
    // Convenience: raster device lookup (nullptr for groups/missing).
    TiledDevice* LayerDevice(LayerId id);
    const TiledDevice* LayerDevice(LayerId id) const;

    // Bottom-up composite of all visible raster leaves into a
    // canvas-sized straight-alpha buffer, honoring group isolation
    // (children of a group composite into a scratch first, then the
    // group result blends with the group's blend mode + opacity).
    // FlattenU32 returns the Skia-bridge packing (R<<24|G<<16|B<<8|A);
    // Flatten wraps it into image::ImageBuffer (RGBA8) for
    // ImageSaver/viewer hand-off.
    std::vector<uint32_t> FlattenU32() const;
    image::ImageBuffer Flatten() const;

    // PNG I/O. OpenPng tries image::ImageLoader (magic-detect decoder
    // chain) and falls back to the Skia bridge codec; SavePng goes
    // through image::ImageSaver (FFmpeg png encoder).
    static std::unique_ptr<Document> OpenPng(const std::string& utf8_path,
                                             std::string* err = nullptr);
    bool SavePng(const std::string& utf8_path,
                 std::string* err = nullptr) const;

private:
    int width_ = 0;
    int height_ = 0;
    image::ColorSpace working_ = image::ColorSpace::sRGB;
    LayerId next_id_ = 1;
    std::unique_ptr<LayerNode> root_;
};

// Renders one subtree's CONTENT into a w*h straight-alpha
// bridge-packed buffer at document origin, with group isolation:
// for a group this is its children composited together (before the
// group's own blend/opacity — the caller applies those when landing
// the buffer); for a raster node it is the raw device pixels. This is
// the scratch image FlattenU32 builds for each group and MergeDown
// uses to merge a group into the layer below.
std::vector<uint32_t> CompositeSubtreeU32(const LayerNode& node,
                                          int w, int h);

// --- basic ops -----------------------------------------------------------

// Fill a raster layer with a solid color. Undoable when `hist` is
// given (recorded as one "Fill" step); without it the fill is applied
// directly with no undo record. `clip` is an optional A8 coverage
// device (a selection): each covered pixel lerps toward the fill
// color by coverage/255 — 0 leaves the pixel untouched.
bool FillLayer(Document& doc, LayerId layer, Pixel color,
               PhotoHistory* hist,
               const TiledDevice* clip = nullptr);

// Fill a raster layer with a linear gradient (Skia bridge gradient,
// positions 0..1, bridge-packed colors). Same undo + clip contract.
bool FillLinearGradient(Document& doc, LayerId layer, float x0, float y0,
                        float x1, float y1,
                        const XER::Bridge::Skia::GradientStops& stops,
                        PhotoHistory* hist,
                        const TiledDevice* clip = nullptr);

}  // namespace XER::VideoEditor::Photo
