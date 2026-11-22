// document.h — the M1 photo document: canvas size, working color-space
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
    // kAdjustment / kText / kVector — M2+ per the stack doc.
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
    float Opacity() const { return opacity_; }
    void SetOpacity(float o) { opacity_ = o; }
    const std::string& BlendMode() const { return blend_mode_; }
    void SetBlendMode(const std::string& m) { blend_mode_ = m; }

    // Raster leaves only (nullptr for groups).
    TiledDevice* Device() { return device_.get(); }
    const TiledDevice* Device() const { return device_.get(); }

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
    float opacity_ = 1.0f;
    std::string blend_mode_ = "srcOver";  // Skia blend-mode name
    std::unique_ptr<TiledDevice> device_;
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

    LayerNode* FindLayer(LayerId id);
    const LayerNode* FindLayer(LayerId id) const;
    // Convenience: raster device lookup (nullptr for groups/missing).
    TiledDevice* LayerDevice(LayerId id);
    const TiledDevice* LayerDevice(LayerId id) const;

    // Bottom-up composite of all visible raster leaves into a
    // canvas-sized straight-alpha buffer. FlattenU32 returns the
    // Skia-bridge packing (R<<24|G<<16|B<<8|A); Flatten wraps it into
    // image::ImageBuffer (RGBA8) for ImageSaver/viewer hand-off.
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

// --- basic ops (M1) ------------------------------------------------------

// Fill a raster layer with a solid color. Undoable when `hist` is
// given (recorded as one "Fill" step); without it the fill is applied
// directly with no undo record.
bool FillLayer(Document& doc, LayerId layer, Pixel color,
               PhotoHistory* hist);

// Fill a raster layer with a linear gradient (Skia bridge gradient,
// positions 0..1, bridge-packed colors). Same undo contract.
bool FillLinearGradient(Document& doc, LayerId layer, float x0, float y0,
                        float x1, float y1,
                        const XER::Bridge::Skia::GradientStops& stops,
                        PhotoHistory* hist);

}  // namespace XER::VideoEditor::Photo
