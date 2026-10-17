#include "node_graph/node_graph_history.h"
 
#include <algorithm>
#include <limits>
#include <map>
#include <set>
 
namespace XER::NodeGraph {
 
namespace {
 
class AddNodeCommand : public Command {
public:
    AddNodeCommand(std::string type, double x, double y) : type_(std::move(type)), x_(x), y_(y) {}
    bool Apply(Graph& g) override {
        if (!applied_once_) {
            id_ = g.AddNode(type_, x_, y_, &error_);
            if (id_ == kInvalidNode) return false;
            node_ = *g.FindNode(id_);
            applied_once_ = true;
            return true;
        }
        return g.InsertNode(node_);
    }
    bool Revert(Graph& g) override { return g.RemoveNode(id_); }
    std::string Label() const override { return "Add " + type_; }
    NodeId id() const { return id_; }
    const std::string& error() const { return error_; }
 
private:
    std::string type_;
    double x_, y_;
    bool applied_once_ = false;
    NodeId id_ = kInvalidNode;
    Node node_;
    std::string error_;
};
 
class RemoveNodeCommand : public Command {
public:
    explicit RemoveNodeCommand(NodeId id) : id_(id) {}
    bool Apply(Graph& g) override {
        const Node* n = g.FindNode(id_);
        if (!n) return false;
        node_ = *n;
        links_ = g.LinksOf(id_);
        return g.RemoveNode(id_);
    }
    bool Revert(Graph& g) override {
        if (!g.InsertNode(node_)) return false;
        for (const auto& l : links_) g.InsertLink(l);
        return true;
    }
    std::string Label() const override { return "Remove " + node_.title; }
 
private:
    NodeId id_;
    Node node_;
    std::vector<Link> links_;
};
 
class ConnectCommand : public Command {
public:
    ConnectCommand(PortRef from, PortRef to) : from_(std::move(from)), to_(std::move(to)) {}
    bool Apply(Graph& g) override {
        if (link_.id == kInvalidLink) {
            const LinkId id = g.Connect(from_, to_, &error_, &replaced_);
            if (id == kInvalidLink) return false;
            link_ = *g.FindLink(id);
            return true;
        }
        return g.InsertLink(link_, &error_);
    }
    bool Revert(Graph& g) override {
        if (!g.Disconnect(link_.id)) return false;
        if (replaced_) g.InsertLink(*replaced_);
        return true;
    }
    std::string Label() const override { return "Connect"; }
    LinkId id() const { return link_.id; }
    const std::string& error() const { return error_; }
 
private:
    PortRef from_, to_;
    Link link_;
    std::optional<Link> replaced_;
    std::string error_;
};
 
class DisconnectCommand : public Command {
public:
    explicit DisconnectCommand(LinkId id) : id_(id) {}
    bool Apply(Graph& g) override {
        const Link* l = g.FindLink(id_);
        if (!l) return false;
        link_ = *l;
        return g.Disconnect(id_);
    }
    bool Revert(Graph& g) override { return g.InsertLink(link_); }
    std::string Label() const override { return "Disconnect"; }
 
private:
    LinkId id_;
    Link link_;
};
 
class MoveNodeCommand : public Command {
public:
    MoveNodeCommand(NodeId id, double x, double y) : id_(id), nx_(x), ny_(y) {}
    bool Apply(Graph& g) override {
        const Node* n = g.FindNode(id_);
        if (!n) return false;
        if (!captured_) {
            ox_ = n->x;
            oy_ = n->y;
            captured_ = true;
        }
        return g.MoveNode(id_, nx_, ny_);
    }
    bool Revert(Graph& g) override { return g.MoveNode(id_, ox_, oy_); }
    std::string Label() const override { return "Move"; }
    bool MergeWith(const Command& other) override {
        const auto* m = dynamic_cast<const MoveNodeCommand*>(&other);
        if (!m || m->id_ != id_) return false;
        nx_ = m->nx_;
        ny_ = m->ny_;
        return true;
    }
 
private:
    NodeId id_;
    double nx_, ny_, ox_ = 0, oy_ = 0;
    bool captured_ = false;
};
 
class SetParamCommand : public Command {
public:
    SetParamCommand(NodeId id, std::string name, Value v) : id_(id), name_(std::move(name)), new_(std::move(v)) {}
    bool Apply(Graph& g) override {
        const Node* n = g.FindNode(id_);
        if (!n) return false;
        if (!captured_) {
            auto it = n->params.find(name_);
            old_ = it == n->params.end() ? Value() : it->second;
            captured_ = true;
        }
        return g.SetParam(id_, name_, new_);
    }
    bool Revert(Graph& g) override { return g.SetParam(id_, name_, old_); }
    std::string Label() const override { return "Set " + name_; }
    bool MergeWith(const Command& other) override {
        const auto* s = dynamic_cast<const SetParamCommand*>(&other);
        if (!s || s->id_ != id_ || s->name_ != name_) return false;
        new_ = s->new_;
        return true;
    }
 
private:
    NodeId id_;
    std::string name_;
    Value new_, old_;
    bool captured_ = false;
};
 
// Whole-graph snapshot for structural edits that touch many elements at once.
class SnapshotCommand : public Command {
public:
    SnapshotCommand(std::string label, Graph before, Graph after)
        : label_(std::move(label)), before_(std::move(before)), after_(std::move(after)) {}
    bool Apply(Graph& g) override {
        g = after_;
        return true;
    }
    bool Revert(Graph& g) override {
        g = before_;
        return true;
    }
    std::string Label() const override { return label_; }
 
private:
    std::string label_;
    Graph before_, after_;
};
 
}  // namespace
 
class History::MacroCommand : public Command {
public:
    explicit MacroCommand(std::string label) : label_(std::move(label)) {}
    bool Apply(Graph& g) override {
        for (auto& c : commands_) if (!c->Apply(g)) return false;
        return true;
    }
    bool Revert(Graph& g) override {
        for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) (*it)->Revert(g);
        return true;
    }
    std::string Label() const override { return label_; }
    void Add(std::unique_ptr<Command> c) { commands_.push_back(std::move(c)); }
    bool empty() const { return commands_.empty(); }
 
private:
    std::string label_;
    std::vector<std::unique_ptr<Command>> commands_;
};
 
History::History(Graph& graph, size_t max_depth) : graph_(graph), max_depth_(std::max<size_t>(1, max_depth)) {}
 
History::~History() = default;
 
bool History::Execute(std::unique_ptr<Command> command) {
    if (!command || !command->Apply(graph_)) return false;
    redo_.clear();
    if (!macro_stack_.empty()) {
        macro_stack_.back()->Add(std::move(command));
        return true;
    }
    if (!undo_.empty() && undo_.back()->MergeWith(*command)) return true;
    undo_.push_back(std::move(command));
    while (undo_.size() > max_depth_) undo_.pop_front();
    return true;
}
 
bool History::Undo() {
    if (undo_.empty() || !macro_stack_.empty()) return false;
    auto cmd = std::move(undo_.back());
    undo_.pop_back();
    cmd->Revert(graph_);
    redo_.push_back(std::move(cmd));
    return true;
}
 
bool History::Redo() {
    if (redo_.empty() || !macro_stack_.empty()) return false;
    auto cmd = std::move(redo_.back());
    redo_.pop_back();
    if (!cmd->Apply(graph_)) return false;
    undo_.push_back(std::move(cmd));
    return true;
}
 
std::string History::UndoLabel() const { return undo_.empty() ? "" : undo_.back()->Label(); }
std::string History::RedoLabel() const { return redo_.empty() ? "" : redo_.back()->Label(); }
 
void History::Clear() {
    undo_.clear();
    redo_.clear();
    macro_stack_.clear();
}
 
void History::BeginMacro(const std::string& label) { macro_stack_.push_back(std::make_unique<MacroCommand>(label)); }
 
void History::EndMacro() {
    if (macro_stack_.empty()) return;
    auto macro = std::move(macro_stack_.back());
    macro_stack_.pop_back();
    if (macro->empty()) return;
    if (!macro_stack_.empty()) {
        macro_stack_.back()->Add(std::move(macro));
        return;
    }
    undo_.push_back(std::move(macro));
    while (undo_.size() > max_depth_) undo_.pop_front();
}
 
NodeId History::AddNode(const std::string& type, double x, double y, std::string* error_out) {
    auto cmd = std::make_unique<AddNodeCommand>(type, x, y);
    auto* raw = cmd.get();
    if (!cmd->Apply(graph_)) {
        if (error_out) *error_out = raw->error();
        return kInvalidNode;
    }
    const NodeId id = raw->id();
    redo_.clear();
    if (!macro_stack_.empty()) macro_stack_.back()->Add(std::move(cmd));
    else {
        undo_.push_back(std::move(cmd));
        while (undo_.size() > max_depth_) undo_.pop_front();
    }
    return id;
}
 
bool History::RemoveNode(NodeId id) { return Execute(std::make_unique<RemoveNodeCommand>(id)); }
 
LinkId History::Connect(const PortRef& from, const PortRef& to, std::string* error_out) {
    auto cmd = std::make_unique<ConnectCommand>(from, to);
    auto* raw = cmd.get();
    if (!cmd->Apply(graph_)) {
        if (error_out) *error_out = raw->error();
        return kInvalidLink;
    }
    const LinkId id = raw->id();
    redo_.clear();
    if (!macro_stack_.empty()) macro_stack_.back()->Add(std::move(cmd));
    else {
        undo_.push_back(std::move(cmd));
        while (undo_.size() > max_depth_) undo_.pop_front();
    }
    return id;
}
 
bool History::Disconnect(LinkId id) { return Execute(std::make_unique<DisconnectCommand>(id)); }
 
bool History::MoveNode(NodeId id, double x, double y) { return Execute(std::make_unique<MoveNodeCommand>(id, x, y)); }
 
bool History::SetParam(NodeId id, const std::string& name, Value value) {
    return Execute(std::make_unique<SetParamCommand>(id, name, std::move(value)));
}
 
NodeId History::CollapseToSubgraph(const std::vector<NodeId>& nodes, std::string* error_out) {
    auto fail = [&](const std::string& msg) {
        if (error_out) *error_out = msg;
        return kInvalidNode;
    };
    const std::set<NodeId> selection(nodes.begin(), nodes.end());
    if (selection.empty()) return fail("selection is empty");
    for (NodeId id : selection) if (!graph_.FindNode(id)) return fail("node not found: " + std::to_string(id));
 
    Graph before = graph_;
    Graph after = graph_;
    auto sub = std::make_shared<Graph>(&graph_.registry());
    sub->name = "Subgraph";
 
    double min_x = std::numeric_limits<double>::max(), max_x = std::numeric_limits<double>::lowest();
    double sum_x = 0, sum_y = 0, min_y = std::numeric_limits<double>::max();
    for (NodeId id : selection) {
        const Node& n = *graph_.FindNode(id);
        sub->InsertNode(n);
        min_x = std::min(min_x, n.x);
        max_x = std::max(max_x, n.x);
        min_y = std::min(min_y, n.y);
        sum_x += n.x;
        sum_y += n.y;
    }
 
    struct External { PortRef outer; std::string name; };
    std::vector<External> incoming, outgoing;
    std::map<std::pair<NodeId, std::string>, std::string> input_names, output_names;
    std::set<std::string> used_in, used_out;
    auto unique_name = [](std::set<std::string>& used, std::string base) {
        std::string name = base;
        for (int i = 2; used.count(name); ++i) name = base + std::to_string(i);
        used.insert(name);
        return name;
    };
 
    double in_y = min_y, out_y = min_y;
    for (const auto& [lid, link] : graph_.links()) {
        const bool from_in = selection.count(link.from.node) != 0;
        const bool to_in = selection.count(link.to.node) != 0;
        if (from_in && to_in) {
            sub->InsertLink(link);
        } else if (to_in) {
            auto key = std::make_pair(link.to.node, link.to.port);
            if (!input_names.count(key)) {
                const std::string name = unique_name(used_in, link.to.port);
                input_names[key] = name;
                const NodeId in_id = sub->AddNode("graph.input", min_x - 240, in_y);
                in_y += 90;
                sub->SetParam(in_id, "name", Value(name));
                const PortSpec* spec = graph_.FindNode(link.to.node)->FindInput(link.to.port);
                sub->SetParam(in_id, "type", Value(ValueTypeName(spec ? spec->type : ValueType::kAny)));
                sub->Connect({in_id, "value"}, link.to);
            }
            incoming.push_back({link.from, input_names[key]});
        } else if (from_in) {
            auto key = std::make_pair(link.from.node, link.from.port);
            if (!output_names.count(key)) {
                const std::string name = unique_name(used_out, link.from.port);
                output_names[key] = name;
                const NodeId out_id = sub->AddNode("graph.output", max_x + 240, out_y);
                out_y += 90;
                sub->SetParam(out_id, "name", Value(name));
                const PortSpec* spec = graph_.FindNode(link.from.node)->FindOutput(link.from.port);
                sub->SetParam(out_id, "type", Value(ValueTypeName(spec ? spec->type : ValueType::kAny)));
                sub->Connect(link.from, {out_id, "value"});
            }
            outgoing.push_back({link.to, output_names[key]});
        }
    }
 
    for (NodeId id : selection) after.RemoveNode(id);
    std::string error;
    const NodeId sg = after.AddNode("graph.subgraph", sum_x / selection.size(), sum_y / selection.size(), &error);
    if (sg == kInvalidNode) return fail(error);
    after.FindNode(sg)->subgraph = sub;
    after.RefreshSubgraphPorts(sg);
    for (const auto& e : incoming) after.Connect(e.outer, {sg, e.name});
    for (const auto& e : outgoing) after.Connect({sg, e.name}, e.outer);
 
    if (!Execute(std::make_unique<SnapshotCommand>("Collapse to subgraph", std::move(before), std::move(after)))) {
        return fail("failed to apply collapse");
    }
    return sg;
}
 
}  // namespace XER::NodeGraph