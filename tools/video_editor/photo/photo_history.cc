// photo_history.cc — see photo_history.h. Pattern lifted from
// src/node_graph/node_graph_history.* (Command/History/Macro).
#include "photo/photo_history.h"

#include "photo/document.h"

namespace XER::VideoEditor::Photo {

// A run of commands treated as one undo step (History::BeginMacro).
class PhotoHistory::MacroCommand : public PhotoCommand {
public:
    explicit MacroCommand(std::string label) : label_(std::move(label)) {}
    void Add(std::unique_ptr<PhotoCommand> c) {
        cmds_.push_back(std::move(c));
    }
    bool Empty() const { return cmds_.empty(); }
    bool Apply(Document& doc) override {
        for (auto& c : cmds_) {
            if (!c->Apply(doc)) return false;
        }
        return true;
    }
    bool Revert(Document& doc) override {
        for (auto it = cmds_.rbegin(); it != cmds_.rend(); ++it) {
            if (!(*it)->Revert(doc)) return false;
        }
        return true;
    }
    std::string Label() const override { return label_; }

private:
    std::string label_;
    std::vector<std::unique_ptr<PhotoCommand>> cmds_;
};

PhotoHistory::PhotoHistory(Document& doc, size_t max_depth)
    : doc_(doc), max_depth_(max_depth ? max_depth : 1) {}

PhotoHistory::~PhotoHistory() = default;

void PhotoHistory::PushUndo(std::unique_ptr<PhotoCommand> command) {
    redo_.clear();
    if (!macro_stack_.empty()) {
        macro_stack_.back()->Add(std::move(command));
        return;
    }
    // Merge continuous edits (attribute drags) when both sides agree.
    if (!undo_.empty() && undo_.back()->MergeWith(*command)) {
        return;
    }
    undo_.push_back(std::move(command));
    while (undo_.size() > max_depth_) undo_.pop_front();
}

bool PhotoHistory::Execute(std::unique_ptr<PhotoCommand> command) {
    if (!command || !command->Apply(doc_)) return false;
    PushUndo(std::move(command));
    return true;
}

bool PhotoHistory::PushApplied(std::unique_ptr<PhotoCommand> command) {
    if (!command) return false;
    PushUndo(std::move(command));
    return true;
}

bool PhotoHistory::Undo() {
    if (undo_.empty()) return false;
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    if (!cmd->Revert(doc_)) {
        redo_.push_back(std::move(cmd));
        return false;
    }
    redo_.push_back(std::move(cmd));
    return true;
}

bool PhotoHistory::Redo() {
    if (redo_.empty()) return false;
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    if (!cmd->Apply(doc_)) {
        redo_.push_back(std::move(cmd));
        return false;
    }
    undo_.push_back(std::move(cmd));
    return true;
}

std::string PhotoHistory::UndoLabel() const {
    return undo_.empty() ? std::string() : undo_.back()->Label();
}

std::string PhotoHistory::RedoLabel() const {
    return redo_.empty() ? std::string() : redo_.back()->Label();
}

void PhotoHistory::Clear() {
    undo_.clear();
    redo_.clear();
    macro_stack_.clear();
}

void PhotoHistory::BeginMacro(const std::string& label) {
    macro_stack_.push_back(std::make_unique<MacroCommand>(label));
}

void PhotoHistory::EndMacro() {
    if (macro_stack_.empty()) return;
    auto macro = std::move(macro_stack_.back());
    macro_stack_.pop_back();
    if (macro->Empty()) return;
    // A macro inside a macro collapses into the parent run.
    if (!macro_stack_.empty()) {
        macro_stack_.back()->Add(std::move(macro));
        return;
    }
    undo_.push_back(std::move(macro));
    while (undo_.size() > max_depth_) undo_.pop_front();
}

bool PhotoHistory::ExecutePaintOp(
    LayerId layer, const std::string& label,
    const std::function<void(TiledDevice&)>& fn) {
    TiledDevice* dev = doc_.LayerDevice(layer);
    if (dev == nullptr || !fn) return false;
    dev->BeginTransaction();
    fn(*dev);
    std::unique_ptr<TileMemento> memento = dev->EndTransaction();
    if (!memento || memento->Empty()) return true;  // no pixels touched
    return PushApplied(std::make_unique<TileMementoCommand>(
        layer, label, std::move(memento)));
}

// --- TileMementoCommand ---------------------------------------------------

TileMementoCommand::TileMementoCommand(
    LayerId layer, std::string label,
    std::unique_ptr<TileMemento> back_memento)
    : layer_(layer), label_(std::move(label)),
      back_(std::move(back_memento)) {}

bool TileMementoCommand::Apply(Document& doc) {
    // Redo path (the op itself was already applied via PushApplied).
    if (!fwd_) return true;  // nothing recorded yet — already applied
    TiledDevice* dev = doc.LayerDevice(layer_);
    if (dev == nullptr) return false;
    back_ = dev->ApplyMemento(*fwd_);
    return true;
}

bool TileMementoCommand::Revert(Document& doc) {
    TiledDevice* dev = doc.LayerDevice(layer_);
    if (dev == nullptr || !back_) return false;
    fwd_ = dev->ApplyMemento(*back_);
    return true;
}

// --- AddLayerCommand -------------------------------------------------------

AddLayerCommand::AddLayerCommand(LayerId parent,
                               std::unique_ptr<LayerNode> node,
                               std::string label)
    : parent_(parent), node_(std::move(node)), label_(std::move(label)) {
    if (node_) node_id_ = node_->Id();
}

bool AddLayerCommand::Apply(Document& doc) {
    if (!node_) return false;
    LayerNode* parent = doc.FindLayer(parent_);
    doc.AdoptLayer(parent, std::move(node_));
    return true;
}

bool AddLayerCommand::Revert(Document& doc) {
    node_ = doc.DetachLayer(node_id_);
    return node_ != nullptr;
}

// --- SetLayerAttributesCommand ----------------------------------------------

SetLayerAttributesCommand::SetLayerAttributesCommand(
    LayerId layer, Attributes next, std::string label)
    : layer_(layer), next_(std::move(next)), label_(std::move(label)) {}

bool SetLayerAttributesCommand::Apply(Document& doc) {
    LayerNode* n = doc.FindLayer(layer_);
    if (n == nullptr) return false;
    if (!have_prev_) {
        prev_ = {n->Name(), n->Visible(), n->Locked(), n->Opacity(),
                 n->BlendMode()};
        have_prev_ = true;
    }
    n->SetName(next_.name);
    n->SetVisible(next_.visible);
    n->SetLocked(next_.locked);
    n->SetOpacity(next_.opacity);
    n->SetBlendMode(next_.blend_mode);
    return true;
}

bool SetLayerAttributesCommand::Revert(Document& doc) {
    LayerNode* n = doc.FindLayer(layer_);
    if (n == nullptr || !have_prev_) return false;
    n->SetName(prev_.name);
    n->SetVisible(prev_.visible);
    n->SetLocked(prev_.locked);
    n->SetOpacity(prev_.opacity);
    n->SetBlendMode(prev_.blend_mode);
    return true;
}

bool SetLayerAttributesCommand::MergeWith(const PhotoCommand& other) {
    const auto* o = dynamic_cast<const SetLayerAttributesCommand*>(&other);
    if (o == nullptr || o->layer_ != layer_) return false;
    next_ = o->next_;  // coalesce drags into the latest target state
    return true;
}

}  // namespace XER::VideoEditor::Photo
