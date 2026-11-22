// document.cc — see document.h.
#include "photo/document.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <iterator>

#include "helper/path_utils.h"
#include "image/io/image_loader.h"
#include "image/io/image_saver.h"
#include "photo/adjust_ops.h"
#include "photo/photo_history.h"

namespace XER::VideoEditor::Photo {

void LayerNode::SetMask(std::unique_ptr<TiledDevice> mask) {
    mask_ = std::move(mask);
}

std::unique_ptr<TiledDevice> LayerNode::TakeMask() {
    return std::move(mask_);
}

namespace {

// Bounding rect (document coords) of the visible painted content of a
// subtree — the raster extents of every visible descendant, clipped
// to `limit`. Empty means the subtree paints nothing.
IntRect CollectContentExtent(const LayerNode& node,
                             const IntRect& limit) {
    IntRect ext;
    if (!node.Visible() || node.Opacity() <= 0.0f) return ext;
    if (node.Kind() == LayerKind::kRaster && node.Device() != nullptr) {
        ext = IntRect::Union(ext, node.Device()->Extent());
    }
    for (const auto& child : node.Children()) {
        ext = IntRect::Union(ext, CollectContentExtent(*child, limit));
    }
    return IntRect::Intersect(ext, limit);
}

// Bounding rect of the non-transparent pixels of `buf` (buf coords).
// Empty = fully transparent.
IntRect AlphaExtent(const std::vector<uint32_t>& buf, int w, int h) {
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y) {
        const uint32_t* row = buf.data() + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            if ((row[x] & 0xFFu) == 0) continue;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    }
    if (x1 < 0) return {};
    return {x0, y0, x1 - x0 + 1, y1 - y0 + 1};
}

// Scales the alpha of every pixel of `px` (w x h buffer whose pixel
// (0,0) sits at document point (ox,oy)) by the mask coverage at the
// same document position — the premultiplied "multiply coverage"
// step reduced to alpha scaling on straight-alpha storage.
void ApplyMaskToU32(std::vector<uint32_t>* px, int w, int h,
                    int ox, int oy, const TiledDevice& mask) {
    const std::vector<uint8_t> cov =
        mask.ReadRegionCoverage(IntRect{ox, oy, w, h});
    for (size_t i = 0; i < px->size(); ++i) {
        if (cov[i] < 255) {
            (*px)[i] = ScaleU32Alpha((*px)[i], cov[i]);
        }
    }
}

// Composites `node` into `dst` (dst_w x dst_h, bridge-packed) whose
// pixel (0,0) sits at document point (dst_ox,dst_oy). Raster leaves
// blend with their own blend mode; groups isolate their children
// into a scratch covering the group content extent, then land the
// scratch with the group's blend mode and accumulated opacity.
// M4: an A8 mask (any node kind) attenuates the composited pixels;
// kAdjustment nodes evaluate their op on the composite-of-below (the
// compositor is_adjustment snapshot mechanic from compositor.h,
// retargeted at the layer stack).
void CompositeNode(const LayerNode& node, float acc_opacity,
                   std::vector<uint32_t>* dst, int dst_w, int dst_h,
                   int dst_ox, int dst_oy) {
    if (!node.Visible()) return;
    const float op = acc_opacity * node.Opacity();
    if (op <= 0.0f) return;
    if (node.Kind() == LayerKind::kRaster && node.Device() != nullptr) {
        const IntRect ext = node.Device()->Extent();
        if (!ext.IsEmpty()) {
            std::vector<uint32_t> src =
                node.Device()->ReadRegionU32(ext);
            if (node.Mask() != nullptr) {
                ApplyMaskToU32(&src, ext.w, ext.h, ext.x, ext.y,
                               *node.Mask());
            }
            XER::Bridge::Skia::ImageComposite(
                dst, dst_w, dst_h, src, ext.w, ext.h, ext.x - dst_ox,
                ext.y - dst_oy, node.BlendMode(), op);
        }
        return;
    }
    if (node.Kind() == LayerKind::kAdjustment) {
        // Snapshot the composite-of-below (dst's current contents in
        // this group's context), evaluate the op on it, and blend the
        // result back at opacity * mask coverage.
        const IntRect ext = AlphaExtent(*dst, dst_w, dst_h);
        if (ext.IsEmpty()) return;
        std::vector<uint32_t> before(
            static_cast<size_t>(ext.w) * ext.h);
        for (int y = 0; y < ext.h; ++y) {
            std::copy_n(
                dst->data() +
                    static_cast<size_t>(ext.y + y) * dst_w + ext.x,
                ext.w,
                before.data() + static_cast<size_t>(y) * ext.w);
        }
        std::vector<uint32_t> after = before;
        if (!ApplyAdjustment(node.Adjustment(), &after, ext.w,
                             ext.h)) {
            return;  // unknown/no-op — the composite stays untouched
        }
        const TiledDevice* mask = node.Mask();
        const IntRect cov_rect{ext.x + dst_ox, ext.y + dst_oy, ext.w,
                               ext.h};
        const std::vector<uint8_t> cov =
            mask != nullptr ? mask->ReadRegionCoverage(cov_rect)
                            : std::vector<uint8_t>();
        for (int y = 0; y < ext.h; ++y) {
            for (int x = 0; x < ext.w; ++x) {
                const size_t i =
                    static_cast<size_t>(ext.y + y) * dst_w + ext.x + x;
                const size_t si = static_cast<size_t>(y) * ext.w + x;
                float k = op;
                if (mask != nullptr) {
                    k *= static_cast<float>(cov[si]) / 255.0f;
                }
                const int ki = static_cast<int>(
                    std::lround(k * 255.0f));
                if (ki <= 0) continue;
                if (ki >= 255) {
                    (*dst)[i] = after[si];
                } else {
                    (*dst)[i] = LerpU32Cov(
                        before[si], after[si],
                        static_cast<uint8_t>(ki));
                }
            }
        }
        return;
    }
    if (node.Kind() == LayerKind::kGroup) {
        const IntRect ext = CollectContentExtent(
            node, IntRect{dst_ox, dst_oy, dst_w, dst_h});
        if (ext.IsEmpty()) return;
        std::vector<uint32_t> scratch(
            static_cast<size_t>(ext.w) * ext.h, 0);
        for (const auto& child : node.Children()) {
            CompositeNode(*child, 1.0f, &scratch, ext.w, ext.h, ext.x,
                          ext.y);
        }
        if (node.Mask() != nullptr) {
            ApplyMaskToU32(&scratch, ext.w, ext.h, ext.x, ext.y,
                           *node.Mask());
        }
        XER::Bridge::Skia::ImageComposite(
            dst, dst_w, dst_h, scratch, ext.w, ext.h, ext.x - dst_ox,
            ext.y - dst_oy, node.BlendMode(), op);
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
    return InsertChildAt(parent,
                         parent ? parent->children_.size()
                                : static_cast<size_t>(-1),
                         std::move(node));
}

int Document::ChildIndex(const LayerNode& parent, LayerId child) const {
    for (size_t i = 0; i < parent.children_.size(); ++i) {
        if (parent.children_[i]->Id() == child) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

LayerNode& Document::InsertChildAt(LayerNode* parent, size_t index,
                                   std::unique_ptr<LayerNode> node) {
    if (parent == nullptr) parent = root_.get();
    if (index > parent->children_.size()) {
        index = parent->children_.size();
    }
    node->parent_ = parent;
    auto it = parent->children_.insert(
        parent->children_.begin() + static_cast<ptrdiff_t>(index),
        std::move(node));
    return **it;
}

std::unique_ptr<LayerNode> Document::CloneSubtree(LayerId id) {
    const LayerNode* src = FindLayer(id);
    if (src == nullptr) return nullptr;
    const auto clone = [this](const LayerNode& s) {
        std::unique_ptr<LayerNode> node(
            new LayerNode(next_id_++, s.kind_, s.name_));
        node->visible_ = s.visible_;
        node->locked_ = s.locked_;
        node->opacity_ = s.opacity_;
        node->blend_mode_ = s.blend_mode_;
        node->adjustment_ = s.adjustment_;
        if (s.device_ != nullptr) {
            node->device_ = s.device_->Clone();  // COW tile sharing
        }
        if (s.mask_ != nullptr) {
            node->mask_ = s.mask_->Clone();
        }
        for (const auto& child : s.children_) {
            node->children_.push_back(
                this->CloneSubtree(child->Id()));
            node->children_.back()->parent_ = node.get();
        }
        return node;
    };
    return clone(*src);
}

std::vector<std::unique_ptr<LayerNode>> Document::DetachAllChildren(
    LayerNode* parent) {
    if (parent == nullptr) return {};
    std::vector<std::unique_ptr<LayerNode>> out =
        std::move(parent->children_);
    parent->children_.clear();
    for (auto& child : out) child->parent_ = nullptr;
    return out;
}

void Document::AdoptChildren(
    LayerNode* parent,
    std::vector<std::unique_ptr<LayerNode>> children) {
    if (parent == nullptr) parent = root_.get();
    for (auto& child : children) {
        InsertChildAt(parent, parent->children_.size(),
                      std::move(child));
    }
}

bool Document::IsDescendantOrSelf(const LayerNode& node,
                                  const LayerNode& ancestor) {
    const LayerNode* p = &node;
    while (p != nullptr) {
        if (p == &ancestor) return true;
        p = p->Parent();
    }
    return false;
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
    if (!root_) return canvas;
    for (const auto& child : root_->Children()) {
        CompositeNode(*child, 1.0f, &canvas, width_, height_, 0, 0);
    }
    return canvas;
}

std::vector<uint32_t> CompositeSubtreeU32(const LayerNode& node,
                                          int w, int h) {
    std::vector<uint32_t> buf(static_cast<size_t>(w) * h, 0);
    if (!node.Visible() || w <= 0 || h <= 0) return buf;
    if (node.Kind() == LayerKind::kRaster) {
        // Raw device pixels masked by the node's A8 mask — own
        // blend/opacity are the caller's to apply when the buffer
        // lands.
        const TiledDevice* dev = node.Device();
        if (dev == nullptr) return buf;
        const IntRect ext = IntRect::Intersect(
            dev->Extent(), IntRect{0, 0, w, h});
        if (ext.IsEmpty()) return buf;
        std::vector<uint32_t> src = dev->ReadRegionU32(ext);
        if (node.Mask() != nullptr) {
            ApplyMaskToU32(&src, ext.w, ext.h, ext.x, ext.y,
                           *node.Mask());
        }
        for (int y = 0; y < ext.h; ++y) {
            std::copy_n(
                src.data() + static_cast<size_t>(y) * ext.w, ext.w,
                buf.data() + static_cast<size_t>(ext.y + y) * w +
                    static_cast<size_t>(ext.x));
        }
        return buf;
    }
    for (const auto& child : node.Children()) {
        CompositeNode(*child, 1.0f, &buf, w, h, 0, 0);
    }
    return buf;
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

namespace {

// Selection-clipped fill worker: lerps every covered pixel of `d`
// toward `src(px)` by clip coverage. `clip` is a same-canvas A8
// device; coverage 0 leaves the pixel untouched.
void MaskedFillOp(TiledDevice& d, const TiledDevice& clip,
                  const std::function<Pixel(int, int)>& src) {
    const IntRect r =
        IntRect::Intersect(clip.Bounds(), d.Bounds());
    for (int y = r.y; y < r.Bottom(); ++y) {
        for (int x = r.x; x < r.Right(); ++x) {
            const uint8_t cov = clip.ReadCoverage(x, y);
            if (cov == 0) continue;
            const Pixel cur = d.ReadPixel(x, y);
            d.WritePixel(x, y, LerpPixelCov(cur, src(x, y), cov));
        }
    }
}

}  // namespace

bool FillLayer(Document& doc, LayerId layer, Pixel color,
               PhotoHistory* hist, const TiledDevice* clip) {
    LayerNode* node = doc.FindLayer(layer);
    if (node == nullptr || node->Locked()) return false;
    TiledDevice* dev = node->Device();
    if (dev == nullptr) return false;
    const auto op = [color, clip](TiledDevice& d) {
        if (clip == nullptr) {
            d.Fill(d.Bounds(), color);
        } else {
            MaskedFillOp(d, *clip,
                         [color](int, int) { return color; });
        }
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, "Fill", op);
    }
    op(*dev);
    return true;
}

bool FillLinearGradient(Document& doc, LayerId layer, float x0, float y0,
                        float x1, float y1,
                        const XER::Bridge::Skia::GradientStops& stops,
                        PhotoHistory* hist,
                        const TiledDevice* clip) {
    LayerNode* node = doc.FindLayer(layer);
    if (node == nullptr || node->Locked()) return false;
    TiledDevice* dev = node->Device();
    if (dev == nullptr) return false;
    const int w = doc.CanvasWidth();
    const int h = doc.CanvasHeight();
    std::vector<uint32_t> grad(static_cast<size_t>(w) * h, 0);
    if (!XER::Bridge::Skia::RasterLinearGradient(&grad, w, h, x0, y0, x1,
                                                 y1, stops, "srcOver")) {
        return false;
    }
    const auto op = [&grad, w, clip](TiledDevice& d) {
        if (clip == nullptr) {
            d.WriteRegionU32(d.Bounds(), grad);
        } else {
            MaskedFillOp(d, *clip, [&grad, w](int x, int y) {
                return Pixel::FromU32(
                    grad[static_cast<size_t>(y) * w + x]);
            });
        }
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, "Gradient Fill", op);
    }
    op(*dev);
    return true;
}

}  // namespace XER::VideoEditor::Photo
