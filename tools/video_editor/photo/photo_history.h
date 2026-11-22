// photo_history.h — undo/redo for the photo document. Mirrors the
// Command{Apply/Revert/MergeWith} + History{Execute/Undo/Redo/
// BeginMacro} pattern from src/node_graph/node_graph_history.h,
// retargeted at Document and split per the Krita model:
//   * structural edits (add/remove/rename layer) are Commands;
//   * paint ops are tile transactions whose TileMemento is pushed as
//     an already-applied command — undo costs only the touched tiles.
#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "photo/tile_store.h"

namespace XER::VideoEditor::Photo {

class Document;
class LayerNode;
using LayerId = uint64_t;

class PhotoCommand {
public:
    virtual ~PhotoCommand() = default;
    virtual bool Apply(Document& doc) = 0;
    virtual bool Revert(Document& doc) = 0;
    virtual std::string Label() const = 0;
    // Allows coalescing continuous edits (e.g. dragging opacity).
    virtual bool MergeWith(const PhotoCommand&) { return false; }
};

// Undo/redo stack. All edits made through PhotoHistory are reversible.
class PhotoHistory {
public:
    explicit PhotoHistory(Document& doc, size_t max_depth = 256);
    ~PhotoHistory();  // in .cc: MacroCommand is incomplete here

    // Runs Apply() and pushes the command on the undo stack.
    bool Execute(std::unique_ptr<PhotoCommand> command);
    // Pushes a command whose effect is ALREADY applied — the Krita
    // transaction path (a stroke paints live, then its tile memento is
    // recorded for undo). The command's Apply() is the redo path only.
    bool PushApplied(std::unique_ptr<PhotoCommand> command);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    std::string UndoLabel() const;
    std::string RedoLabel() const;
    void Clear();

    // Groups subsequent commands into one undo step (Begin/End nest).
    void BeginMacro(const std::string& label);
    void EndMacro();

    // Krita-style paint op: opens a tile transaction on the layer's
    // device, runs `fn`, closes it, and records the captured
    // TileMemento as one undoable step. fn may mutate the device only.
    bool ExecutePaintOp(LayerId layer, const std::string& label,
                        const std::function<void(TiledDevice&)>& fn);

private:
    class MacroCommand;
    void PushUndo(std::unique_ptr<PhotoCommand> command);

    Document& doc_;
    size_t max_depth_;
    std::deque<std::unique_ptr<PhotoCommand>> undo_;
    std::vector<std::unique_ptr<PhotoCommand>> redo_;
    std::vector<std::unique_ptr<MacroCommand>> macro_stack_;
};

// --- concrete commands --------------------------------------------------

// Undo for a tile-level mutation (stroke, fill, erase, composite).
// `back_memento` is the transaction delta captured at paint time;
// Revert swaps it in and keeps the inverse for Redo.
class TileMementoCommand : public PhotoCommand {
public:
    TileMementoCommand(LayerId layer, std::string label,
                       std::unique_ptr<TileMemento> back_memento);
    bool Apply(Document& doc) override;    // redo: re-apply fwd state
    bool Revert(Document& doc) override;   // undo: restore pre-op tiles
    std::string Label() const override { return label_; }

private:
    LayerId layer_;
    std::string label_;
    std::unique_ptr<TileMemento> back_;  // pre-op state (for undo)
    std::unique_ptr<TileMemento> fwd_;   // post-op state (for redo)
};

// Structural: inserts an already-built layer subtree under a parent.
class AddLayerCommand : public PhotoCommand {
public:
    AddLayerCommand(LayerId parent, std::unique_ptr<LayerNode> node,
                    std::string label = "Add Layer");
    bool Apply(Document& doc) override;
    bool Revert(Document& doc) override;
    std::string Label() const override { return label_; }

private:
    LayerId parent_;
    std::unique_ptr<LayerNode> node_;  // owned while un-done
    LayerId node_id_ = 0;
    std::string label_;
};

// Structural: rename / visibility / lock / opacity / blend-mode edit.
class SetLayerAttributesCommand : public PhotoCommand {
public:
    struct Attributes {
        std::string name;
        bool visible = true;
        bool locked = false;
        float opacity = 1.0f;
        std::string blend_mode = "srcOver";
    };
    SetLayerAttributesCommand(LayerId layer, Attributes next,
                              std::string label = "Layer Attributes");
    bool Apply(Document& doc) override;
    bool Revert(Document& doc) override;
    std::string Label() const override { return label_; }
    bool MergeWith(const PhotoCommand& other) override;

private:
    LayerId layer_;
    Attributes prev_, next_;
    bool have_prev_ = false;
    std::string label_;
};

}  // namespace XER::VideoEditor::Photo
