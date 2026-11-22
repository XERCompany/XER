// document.cc — see document.h.
#include "photo/document.h"

#include <algorithm>
#include <fstream>
#include <iterator>

#include "helper/path_utils.h"
#include "image/io/image_loader.h"
#include "image/io/image_saver.h"
#include "photo/photo_history.h"

namespace XER::VideoEditor::Photo {

namespace {

void FlattenNode(const LayerNode& node, float acc_opacity,
                 std::vector<uint32_t>* canvas, int w, int h) {
    if (!node.Visible()) return;
    const float op = acc_opacity * node.Opacity();
    if (op <= 0.0f) return;
    if (node.Kind() == LayerKind::kRaster && node.Device() != nullptr) {
        const IntRect ext = node.Device()->Extent();
        if (!ext.IsEmpty()) {
            const std::vector<uint32_t> src =
                node.Device()->ReadRegionU32(ext);
            XER::Bridge::Skia::ImageComposite(canvas, w, h, src, ext.w,
                                              ext.h, ext.x, ext.y,
                                              node.BlendMode(), op);
        }
    }
    for (const auto& child : node.Children()) {
        FlattenNode(*child, op, canvas, w, h);
    }
}

const LayerNode* FindIn(const LayerNode& node, LayerId id) {
    if (node.Id() == id) return &node;
    for (const auto& child : node.Children()) {
        if (const LayerNode* hit = FindIn(*child, id)) return hit;
    }
    return nullptr;
}

}  // namespace

Document::Document(int canvas_w, int canvas_h, image::ColorSpace working)
    : width_(canvas_w < 0 ? 0 : canvas_w),
      height_(canvas_h < 0 ? 0 : canvas_h),
      working_(working) {
    // LayerNode's ctor is private (tree ownership stays with Document),
    // so new is used instead of make_unique — access checks inside
    // std::make_unique would not see this class's friendship.
    root_ = std::unique_ptr<LayerNode>(
        new LayerNode(next_id_++, LayerKind::kGroup, "root"));
}

LayerNode& Document::AddLayer(LayerKind kind, const std::string& name,
                              LayerNode* parent) {
    if (parent == nullptr) parent = root_.get();
    std::unique_ptr<LayerNode> node(
        new LayerNode(next_id_++, kind, name));
    node->parent_ = parent;
    if (kind == LayerKind::kRaster) {
        node->device_ = std::make_unique<TiledDevice>(
            width_, height_, image::PixelFormat::RGBA8,
            Pixel::Transparent());
    }
    parent->children_.push_back(std::move(node));
    return *parent->children_.back();
}

std::unique_ptr<LayerNode> Document::DetachLayer(LayerId id) {
    LayerNode* node = FindLayer(id);
    if (node == nullptr || node == root_.get() || node->parent_ == nullptr) {
        return nullptr;
    }
    auto& siblings = node->parent_->children_;
    for (auto it = siblings.begin(); it != siblings.end(); ++it) {
        if (it->get() == node) {
            std::unique_ptr<LayerNode> owned = std::move(*it);
            siblings.erase(it);
            owned->parent_ = nullptr;
            return owned;
        }
    }
    return nullptr;
}

LayerNode& Document::AdoptLayer(LayerNode* parent,
                              std::unique_ptr<LayerNode> node) {
    if (parent == nullptr) parent = root_.get();
    node->parent_ = parent;
    parent->children_.push_back(std::move(node));
    return *parent->children_.back();
}

LayerNode* Document::FindLayer(LayerId id) {
    const LayerNode* hit = root_ ? FindIn(*root_, id) : nullptr;
    return const_cast<LayerNode*>(hit);
}

const LayerNode* Document::FindLayer(LayerId id) const {
    return root_ ? FindIn(*root_, id) : nullptr;
}

TiledDevice* Document::LayerDevice(LayerId id) {
    LayerNode* n = FindLayer(id);
    return (n && n->Kind() == LayerKind::kRaster) ? n->Device() : nullptr;
}

const TiledDevice* Document::LayerDevice(LayerId id) const {
    const LayerNode* n = FindLayer(id);
    return (n && n->Kind() == LayerKind::kRaster) ? n->Device() : nullptr;
}

std::vector<uint32_t> Document::FlattenU32() const {
    std::vector<uint32_t> canvas(
        static_cast<size_t>(width_) * height_, 0);
    if (root_) FlattenNode(*root_, 1.0f, &canvas, width_, height_);
    return canvas;
}

image::ImageBuffer Document::Flatten() const {
    image::ImageBuffer out(width_, height_, image::PixelFormat::RGBA8,
                           working_);
    const std::vector<uint32_t> px = FlattenU32();
    uint8_t* dst = out.Data();
    for (size_t i = 0; i < px.size(); ++i, dst += 4) {
        dst[0] = static_cast<uint8_t>((px[i] >> 24) & 0xFF);
        dst[1] = static_cast<uint8_t>((px[i] >> 16) & 0xFF);
        dst[2] = static_cast<uint8_t>((px[i] >> 8) & 0xFF);
        dst[3] = static_cast<uint8_t>(px[i] & 0xFF);
    }
    return out;
}

std::unique_ptr<Document> Document::OpenPng(const std::string& utf8_path,
                                            std::string* err) {
    // Primary: src/image decoder chain (magic-detect — PNG, JPEG,
    // WebP, TIFF, PSD-flattened, ... all ride the same call).
    if (auto buf = image::ImageLoader().Load(utf8_path);
        buf && buf->IsValid() &&
        buf->Format() == image::PixelFormat::RGBA8) {
        auto doc = std::make_unique<Document>(buf->Width(), buf->Height(),
                                              buf->GetColorSpace());
        LayerNode& bg = doc->AddRasterLayer("Background");
        bg.Device()->WriteRegion(doc->CanvasRect(), buf->Data());
        return doc;
    }

    // Fallback: Skia bridge codec over file bytes.
    std::ifstream f(XER::Helper::Path::FromUtf8(utf8_path),
                    std::ios::binary);
    if (!f) {
        if (err) *err = "cannot open file";
        return nullptr;
    }
    const std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f),
                                     std::istreambuf_iterator<char>()};
    int w = 0, h = 0;
    std::vector<uint32_t> px;
    if (!XER::Bridge::Skia::DecodeImage(bytes, &w, &h, &px) || w <= 0 ||
        h <= 0) {
        if (err) *err = "decode failed";
        return nullptr;
    }
    auto doc = std::make_unique<Document>(w, h);
    LayerNode& bg = doc->AddRasterLayer("Background");
    bg.Device()->WriteRegionU32(doc->CanvasRect(), px);
    return doc;
}

bool Document::SavePng(const std::string& utf8_path,
                       std::string* err) const {
    const image::ImageBuffer flat = Flatten();
    if (!flat.IsValid()) {
        if (err) *err = "empty document";
        return false;
    }
    if (!image::ImageSaver().SavePng(flat, utf8_path)) {
        if (err) *err = "PNG encode failed";
        return false;
    }
    return true;
}

// --- ops -----------------------------------------------------------------

bool FillLayer(Document& doc, LayerId layer, Pixel color,
               PhotoHistory* hist) {
    TiledDevice* dev = doc.LayerDevice(layer);
    if (dev == nullptr) return false;
    const auto op = [color](TiledDevice& d) { d.Fill(d.Bounds(), color); };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, "Fill", op);
    }
    op(*dev);
    return true;
}

bool FillLinearGradient(Document& doc, LayerId layer, float x0, float y0,
                        float x1, float y1,
                        const XER::Bridge::Skia::GradientStops& stops,
                        PhotoHistory* hist) {
    TiledDevice* dev = doc.LayerDevice(layer);
    if (dev == nullptr) return false;
    const int w = doc.CanvasWidth();
    const int h = doc.CanvasHeight();
    std::vector<uint32_t> grad(static_cast<size_t>(w) * h, 0);
    if (!XER::Bridge::Skia::RasterLinearGradient(&grad, w, h, x0, y0, x1,
                                                 y1, stops, "srcOver")) {
        return false;
    }
    const auto op = [&grad](TiledDevice& d) {
        d.WriteRegionU32(d.Bounds(), grad);
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, "Gradient Fill", op);
    }
    op(*dev);
    return true;
}

}  // namespace XER::VideoEditor::Photo
