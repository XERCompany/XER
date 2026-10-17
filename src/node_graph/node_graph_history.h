#pragma once
 
#include "node_graph/node_graph.h"
 
#include <deque>
#include <memory>
#include <string>
#include <vector>
 
namespace XER::NodeGraph {
 
class Command {
public:
    virtual ~Command() = default;
    virtual bool Apply(Graph& graph) = 0;
    virtual bool Revert(Graph& graph) = 0;
    virtual std::string Label() const = 0;
    // Allows coalescing continuous edits (e.g. dragging a node).
    virtual bool MergeWith(const Command&) { return false; }
};
 
// Undo/redo stack. All edits made through History are reversible.
class History {
public:
    explicit History(Graph& graph, size_t max_depth = 256);
    ~History();
 
    bool Execute(std::unique_ptr<Command> command);
    bool Undo();
    bool Redo();
    bool CanUndo() const { return !undo_.empty(); }
    bool CanRedo() const { return !redo_.empty(); }
    std::string UndoLabel() const;
    std::string RedoLabel() const;
    void Clear();
 
    // Groups subsequent commands into one undo step.
    void BeginMacro(const std::string& label);
    void EndMacro();
 
    // Convenience wrappers.
    NodeId AddNode(const std::string& type, double x = 0, double y = 0, std::string* error_out = nullptr);
    bool RemoveNode(NodeId id);
    LinkId Connect(const PortRef& from, const PortRef& to, std::string* error_out = nullptr);
    bool Disconnect(LinkId id);
    bool MoveNode(NodeId id, double x, double y);
    bool SetParam(NodeId id, const std::string& name, Value value);
    // Collapses the given nodes into a subgraph node, rewiring external links
    // through generated graph.input / graph.output nodes.
    NodeId CollapseToSubgraph(const std::vector<NodeId>& nodes, std::string* error_out = nullptr);
 
private:
    class MacroCommand;
 
    Graph& graph_;
    size_t max_depth_;
    std::deque<std::unique_ptr<Command>> undo_;
    std::vector<std::unique_ptr<Command>> redo_;
    std::vector<std::unique_ptr<MacroCommand>> macro_stack_;
};
 
}  // namespace XER::NodeGraph