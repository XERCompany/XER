// layer_ops.cc — see layer_ops.h. The PhotoCommand subclasses are
// file-local: the public surface is the free ops, matching the M1
// FillLayer convention.
#include "photo/layer_ops.h"

#include <cmath>
#include <utility>
#include <vector>

#include "photo/adjust_ops.h"
#include "photo/photo_history.h"

namespace XER::VideoEditor::Photo {

namespace {

// Executes `cmd` through the history, or applies it directly when no
// history is attached (same contract as FillLayer).
bool RunOrRecord(PhotoHistory* hist, Document& doc,
                 std::unique_ptr<PhotoCommand> cmd) {
    if (cmd == nullptr) return false;
    if (hist != nullptr) return hist->Execute(std::move(cmd));
    return cmd->Apply(doc);
}

// ---------------------------------------------------------------------------
// Move / reparent
// ---------------------------------------------------------------------------

class MoveLayerCommand : public PhotoCommand {
public:
    // `new_parent` may be kInvalidLayerId for the document root.
    MoveLayerCommand(LayerId layer, LayerId new_parent, size_t index)
        : layer_(layer), new_parent_(new_parent), new_index_(index) {}

    bool Apply(Document& doc) override {
        LayerNode* node = doc.FindLayer(layer_);
        if (node == nullptr || node == &doc.Root()) return false;
        LayerNode* parent = (new_parent_ == kInvalidLayerId)
                                ? &doc.Root()
                                : doc.FindLayer(new_parent_);
        if (parent == nullptr ||
            parent->Kind() != LayerKind::kGroup) {
            return false;
        }
        // No cycles: the target must not sit inside the moving
        // subtree (nor be the node itself).
        if (Document::IsDescendantOrSelf(*parent, *node)) {
            return false;
        }
        if (!have_prev_) {
            prev_parent_ = node->Parent() ? node->Parent()->Id()
                                          : kInvalidLayerId;
            prev_index_ = static_cast<size_t>(
                doc.ChildIndex(*node->Parent(), layer_));
            have_prev_ = true;
        }
        auto owned = doc.DetachLayer(layer_);
        if (owned == nullptr) return false;
        doc.InsertChildAt(parent, new_index_, std::move(owned));
        return true;
    }

    bool Revert(Document& doc) override {
        if (!have_prev_) return false;
        LayerNode* parent = (prev_parent_ == kInvalidLayerId)
                                ? &doc.Root()
                                : doc.FindLayer(prev_parent_);
        if (parent == nullptr) return false;
        auto owned = doc.DetachLayer(layer_);
        if (owned == nullptr) return false;
        doc.InsertChildAt(parent, prev_index_, std::move(owned));
        return true;
    }

    std::string Label() const override { return "Move Layer"; }

private:
    LayerId layer_;
    LayerId new_parent_;
    size_t new_index_;
    LayerId prev_parent_ = kInvalidLayerId;
    size_t prev_index_ = 0;
    bool have_prev_ = false;
};

// ---------------------------------------------------------------------------
// Merge down
// ---------------------------------------------------------------------------

// Composites the upper subtree's isolated image onto the raster
// sibling directly below it, then removes the upper node. Undo =
// tile memento on the lower device + re-adopting the removed node.
class MergeDownCommand : public PhotoCommand {
public:
    explicit MergeDownCommand(LayerId layer) : upper_id_(layer) {}

    bool Apply(Document& doc) override {
        if (have_record_) {
            // Redo: restore the merged pixels, re-remove the node.
            // (removed_ is null here — Revert adopted it back into
            // the tree; detach captures it again.)
            TiledDevice* dev = doc.LayerDevice(lower_id_);
            if (dev == nullptr || !fwd_) return false;
            back_ = dev->ApplyMemento(*fwd_);
            removed_ = doc.DetachLayer(upper_id_);
            return removed_ != nullptr;
        }
        LayerNode* upper = doc.FindLayer(upper_id_);
        if (upper == nullptr || upper == &doc.Root() ||
            upper->Locked() || upper->Parent() == nullptr) {
            return false;
        }
        LayerNode* parent = upper->Parent();
        const int index = doc.ChildIndex(*parent, upper_id_);
        if (index <= 0) return false;  // nothing below
        LayerNode* lower =
            parent->Children()[static_cast<size_t>(index - 1)].get();
        if (lower->Kind() != LayerKind::kRaster ||
            lower->Device() == nullptr || lower->Locked()) {
            return false;
        }
        TiledDevice* dev = lower->Device();
        dev->BeginTransaction();
        if (upper->Kind() == LayerKind::kAdjustment) {
            // M4: merging an adjustment bakes the op onto the lower
            // layer's pixels (blend = lerp at opacity*mask, the same
            // math the compositor uses when the layer is live).
            const IntRect ext = dev->Extent();
            if (!ext.IsEmpty()) {
                std::vector<uint32_t> before =
                    dev->ReadRegionU32(ext);
                std::vector<uint32_t> after = before;
                if (ApplyAdjustment(upper->Adjustment(), &after, ext.w,
                                    ext.h)) {
                    const TiledDevice* mask = upper->Mask();
                    const std::vector<uint8_t> cov =
                        mask != nullptr
                            ? mask->ReadRegionCoverage(ext)
                            : std::vector<uint8_t>();
                    for (int y = 0; y < ext.h; ++y) {
                        for (int x = 0; x < ext.w; ++x) {
                            const size_t i =
                                static_cast<size_t>(y) * ext.w + x;
                            float k = upper->Opacity();
                            if (mask != nullptr) {
                                k *= static_cast<float>(cov[i]) /
                                     255.0f;
                            }
                            const int ki = static_cast<int>(
                                std::lround(k * 255.0f));
                            if (ki <= 0) continue;
                            if (ki >= 255) {
                                before[i] = after[i];
                            } else {
                                before[i] = LerpU32Cov(
                                    before[i], after[i],
                                    static_cast<uint8_t>(ki));
                            }
                        }
                    }
                    dev->WriteRegionU32(ext, before);
                }
            }
        } else {
            const int w = doc.CanvasWidth();
            const int h = doc.CanvasHeight();
            const std::vector<uint32_t> src =
                CompositeSubtreeU32(*upper, w, h);
            dev->CompositeRect(dev->Bounds(), src, w, h, 0, 0,
                               upper->BlendMode(), upper->Opacity());
        }
        back_ = dev->EndTransaction();
        lower_id_ = lower->Id();
        parent_id_ = parent->Id();
        upper_index_ = static_cast<size_t>(index);
        removed_ = doc.DetachLayer(upper_id_);
        have_record_ = true;
        return removed_ != nullptr;
    }

    bool Revert(Document& doc) override {
        if (!have_record_ || !back_ || !removed_) return false;
        TiledDevice* dev = doc.LayerDevice(lower_id_);
        LayerNode* parent = doc.FindLayer(parent_id_);
        if (dev == nullptr || parent == nullptr) return false;
        fwd_ = dev->ApplyMemento(*back_);
        doc.InsertChildAt(parent, upper_index_, std::move(removed_));
        return true;
    }

    std::string Label() const override { return "Merge Down"; }

private:
    LayerId upper_id_;
    LayerId lower_id_ = kInvalidLayerId;
    LayerId parent_id_ = kInvalidLayerId;
    size_t upper_index_ = 0;
    std::unique_ptr<LayerNode> removed_;
    std::unique_ptr<TileMemento> back_;
    std::unique_ptr<TileMemento> fwd_;
    bool have_record_ = false;
};

// ---------------------------------------------------------------------------
// Flatten image
// ---------------------------------------------------------------------------

// Replaces the root's children with one raster layer holding the
// flattened composite. Undo restores the whole original tree.
class FlattenImageCommand : public PhotoCommand {
public:
    bool Apply(Document& doc) override {
        if (!have_) {
            const std::vector<uint32_t> px = doc.FlattenU32();
            LayerNode& made = doc.AddRasterLayer("Flattened");
            flat_id_ = made.Id();
            flat_ = doc.DetachLayer(flat_id_);
            if (flat_ == nullptr || flat_->Device() == nullptr) {
                return false;
            }
            flat_->Device()->WriteRegionU32(doc.CanvasRect(), px);
            have_ = true;
        }
        if (flat_ == nullptr) return false;
        saved_ = doc.DetachAllChildren(&doc.Root());
        doc.InsertChildAt(&doc.Root(), 0, std::move(flat_));
        return true;
    }

    bool Revert(Document& doc) override {
        if (!have_) return false;
        flat_ = doc.DetachLayer(flat_id_);
        if (flat_ == nullptr) return false;
        doc.AdoptChildren(&doc.Root(), std::move(saved_));
        return true;
    }

    std::string Label() const override { return "Flatten Image"; }

private:
    std::vector<std::unique_ptr<LayerNode>> saved_;
    std::unique_ptr<LayerNode> flat_;
    LayerId flat_id_ = kInvalidLayerId;
    bool have_ = false;
};

// ---------------------------------------------------------------------------
// Duplicate
// ---------------------------------------------------------------------------

class DuplicateLayerCommand : public PhotoCommand {
public:
    explicit DuplicateLayerCommand(LayerId src) : src_(src) {}

    LayerId DuplicatedId() const { return copy_id_; }

    bool Apply(Document& doc) override {
        if (!have_) {
            LayerNode* src = doc.FindLayer(src_);
            if (src == nullptr || src == &doc.Root() ||
                src->Parent() == nullptr) {
                return false;
            }
            copy_ = doc.CloneSubtree(src_);
            if (copy_ == nullptr) return false;
            copy_id_ = copy_->Id();
            parent_id_ = src->Parent()->Id();
            index_ = static_cast<size_t>(
                doc.ChildIndex(*src->Parent(), src_) + 1);
            have_ = true;
        }
        if (copy_ == nullptr) return false;
        LayerNode* parent = doc.FindLayer(parent_id_);
        if (parent == nullptr) return false;
        doc.InsertChildAt(parent, index_, std::move(copy_));
        return true;
    }

    bool Revert(Document& doc) override {
        copy_ = doc.DetachLayer(copy_id_);
        return copy_ != nullptr;
    }

    std::string Label() const override { return "Duplicate Layer"; }

private:
    LayerId src_;
    std::unique_ptr<LayerNode> copy_;
    LayerId copy_id_ = kInvalidLayerId;
    LayerId parent_id_ = kInvalidLayerId;
    size_t index_ = 0;
    bool have_ = false;
};

}  // namespace

// ---------------------------------------------------------------------------
// Public ops
// ---------------------------------------------------------------------------

bool MoveLayer(Document& doc, LayerId layer, LayerId new_parent,
               size_t index, PhotoHistory* hist) {
    return RunOrRecord(hist, doc,
                       std::make_unique<MoveLayerCommand>(
                           layer, new_parent, index));
}

bool MergeDown(Document& doc, LayerId layer, PhotoHistory* hist) {
    return RunOrRecord(
        hist, doc, std::make_unique<MergeDownCommand>(layer));
}

bool FlattenImage(Document& doc, PhotoHistory* hist) {
    return RunOrRecord(hist, doc,
                       std::make_unique<FlattenImageCommand>());
}

LayerId DuplicateLayer(Document& doc, LayerId layer,
                       PhotoHistory* hist) {
    auto cmd = std::make_unique<DuplicateLayerCommand>(layer);
    DuplicateLayerCommand* raw = cmd.get();
    if (!RunOrRecord(hist, doc, std::move(cmd))) {
        return kInvalidLayerId;
    }
    return raw->DuplicatedId();
}

bool TransformLayer(Document& doc, LayerId layer,
                    const XER::Bridge::Skia::Matrix3x3& matrix,
                    PhotoHistory* hist, const std::string& label) {
    LayerNode* node = doc.FindLayer(layer);
    if (node == nullptr || node->Kind() != LayerKind::kRaster ||
        node->Locked() || node->Device() == nullptr) {
        return false;
    }
    // Identity would only round-trip pixels through Skia's premul
    // path for nothing — treat as a successful no-op.
    const XER::Bridge::Skia::Matrix3x3 identity =
        XER::Bridge::Skia::MatrixIdentity();
    if (matrix == identity) return true;

    const int w = doc.CanvasWidth();
    const int h = doc.CanvasHeight();
    if (w <= 0 || h <= 0) return false;
    const std::vector<uint32_t> src =
        node->Device()->ReadRegionU32(IntRect{0, 0, w, h});
    std::vector<uint32_t> dst(static_cast<size_t>(w) * h, 0);
    if (!XER::Bridge::Skia::ImageCompositeWarp(&dst, w, h, src, w, h,
                                               matrix, 1.0f)) {
        return false;
    }
    const auto op = [&dst](TiledDevice& d) {
        d.WriteRegionU32(d.Bounds(), dst);
    };
    if (hist != nullptr) {
        return hist->ExecutePaintOp(layer, label, op);
    }
    op(*node->Device());
    return true;
}

bool MoveLayerPixels(Document& doc, LayerId layer, double dx,
                     double dy, PhotoHistory* hist) {
    return TransformLayer(doc, layer,
                          XER::Bridge::Skia::MatrixTranslate(dx, dy),
                          hist, "Move Layer Pixels");
}

bool ScaleLayerPixels(Document& doc, LayerId layer, double sx,
                      double sy, PhotoHistory* hist) {
    if (sx == 0.0 || sy == 0.0) return false;
    return TransformLayer(doc, layer,
                          XER::Bridge::Skia::MatrixScale(sx, sy),
                          hist, "Scale Layer Pixels");
}

}  // namespace XER::VideoEditor::Photo
