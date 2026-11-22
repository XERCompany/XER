// mask_ops.cc — see mask_ops.h.
#include "photo/mask_ops.h"

#include "photo/photo_history.h"

namespace XER::VideoEditor::Photo {

namespace {

// One command drives both directions: `attach_` picks which way
// Apply() mutates the node. The held device keeps painted content
// alive across undo/redo (and hands the same object back on redo, so
// DeviceMementoCommand pointers into it stay valid).
class MaskAttachCommand : public PhotoCommand {
public:
    MaskAttachCommand(LayerId layer, bool attach)
        : layer_(layer), attach_(attach) {}

    bool Apply(Document& doc) override {
        LayerNode* node = doc.FindLayer(layer_);
        if (node == nullptr) return false;
        if (attach_) {
            if (node->Mask() != nullptr) return false;
            if (held_ != nullptr) {
                node->SetMask(std::move(held_));
            } else {
                // Fresh white A8 mask — default coverage 255.
                node->SetMask(std::make_unique<TiledDevice>(
                    doc.CanvasWidth(), doc.CanvasHeight(),
                    image::PixelFormat::Gray8,
                    Pixel{0, 0, 0, 255}));
            }
            return true;
        }
        if (node->Mask() == nullptr) return false;
        held_ = node->TakeMask();
        return true;
    }

    bool Revert(Document& doc) override {
        LayerNode* node = doc.FindLayer(layer_);
        if (node == nullptr) return false;
        if (attach_) {
            if (node->Mask() == nullptr) return false;
            held_ = node->TakeMask();
            return true;
        }
        if (node->Mask() != nullptr || held_ == nullptr) return false;
        node->SetMask(std::move(held_));
        return true;
    }

    std::string Label() const override {
        return attach_ ? "Add Layer Mask" : "Remove Layer Mask";
    }

private:
    LayerId layer_;
    bool attach_;
    std::unique_ptr<TiledDevice> held_;
};

}  // namespace

bool AttachLayerMask(Document& doc, LayerId layer, PhotoHistory* hist) {
    auto cmd = std::make_unique<MaskAttachCommand>(layer, true);
    if (hist != nullptr) return hist->Execute(std::move(cmd));
    return cmd->Apply(doc);
}

bool DetachLayerMask(Document& doc, LayerId layer, PhotoHistory* hist) {
    auto cmd = std::make_unique<MaskAttachCommand>(layer, false);
    if (hist != nullptr) return hist->Execute(std::move(cmd));
    return cmd->Apply(doc);
}

}  // namespace XER::VideoEditor::Photo
