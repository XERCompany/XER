#include "node_graph/node_graph.h"
 
#include <algorithm>
#include <deque>
 
namespace XER::NodeGraph {
 
const PortSpec* Node::FindInput(const std::string& port) const {
    for (const auto& p : inputs) if (p.name == port) return &p;
    return nullptr;
}
 
const PortSpec* Node::FindOutput(const std::string& port) const {
    for (const auto& p : outputs) if (p.name == port) return &p;
    return nullptr;
}
 
Graph::Graph(NodeTypeRegistry* registry) : registry_(registry ? registry : &NodeTypeRegistry::Shared()) {}
 
Graph::Graph(const Graph& other) { *this = other; }
 
Graph& Graph::operator=(const Graph& other) {
    if (this == &other) return *this;
    registry_ = other.registry_;
    name = other.name;
    nodes_ = other.nodes_;
    for (auto& [id, node] : nodes_) {
        if (node.subgraph) node.subgraph = std::make_shared<Graph>(*node.subgraph);
    }
    links_ = other.links_;
    groups_ = other.groups_;
    dirty_ = other.dirty_;
    next_node_id_ = other.next_node_id_;
    next_link_id_ = other.next_link_id_;
    next_group_id_ = other.next_group_id_;
    revision_ = other.revision_;
    return *this;
}
 
NodeId Graph::AddNode(const std::string& type, double x, double y, std::string* error_out) {
    const NodeTypeDescriptor* desc = registry_->Find(type);
    if (desc == nullptr) {
        if (error_out) *error_out = "unknown node type: " + type;
        return kInvalidNode;
    }
    Node node;
    node.id = next_node_id_;
    node.type = type;
    node.title = desc->title;
    node.x = x;
    node.y = y;
    node.inputs = desc->inputs;
    node.outputs = desc->outputs;
    for (const auto& p : desc->params) node.params[p.name] = p.default_value;
    if (type == "graph.subgraph") node.subgraph = std::make_shared<Graph>(registry_);
    if (!InsertNode(std::move(node), error_out)) return kInvalidNode;
    return next_node_id_ - 1;
}
 
bool Graph::InsertNode(Node node, std::string* error_out) {
    if (node.id == kInvalidNode) node.id = next_node_id_;
    if (nodes_.count(node.id)) {
        if (error_out) *error_out = "duplicate node id " + std::to_string(node.id);
        return false;
    }
    const NodeId id = node.id;
    next_node_id_ = std::max(next_node_id_, id + 1);
    nodes_.emplace(id, std::move(node));
    dirty_.insert(id);
    Touch();
    return true;
}
 
bool Graph::RemoveNode(NodeId id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return false;
    std::set<NodeId> affected;
    for (auto lit = links_.begin(); lit != links_.end();) {
        if (lit->second.from.node == id || lit->second.to.node == id) {
            if (lit->second.from.node == id) affected.insert(lit->second.to.node);
            lit = links_.erase(lit);
        } else {
            ++lit;
        }
    }
    nodes_.erase(it);
    dirty_.erase(id);
    for (NodeId n : affected) MarkDirty(n);
    Touch();
    return true;
}
 
Node* Graph::FindNode(NodeId id) {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}
 
const Node* Graph::FindNode(NodeId id) const {
    auto it = nodes_.find(id);
    return it == nodes_.end() ? nullptr : &it->second;
}
 
bool Graph::MoveNode(NodeId id, double x, double y) {
    Node* node = FindNode(id);
    if (!node) return false;
    node->x = x;
    node->y = y;
    Touch();
    return true;
}
 
bool Graph::SetParam(NodeId id, const std::string& param, Value value) {
    Node* node = FindNode(id);
    if (!node) return false;
    node->params[param] = std::move(value);
    MarkDirty(id);
    Touch();
    return true;
}
 
bool Graph::SetTitle(NodeId id, const std::string& title) {
    Node* node = FindNode(id);
    if (!node) return false;
    node->title = title;
    Touch();
    return true;
}
 
bool Graph::RefreshSubgraphPorts(NodeId id) {
    Node* node = FindNode(id);
    if (!node || !node->subgraph) return false;
    std::vector<std::pair<double, PortSpec>> ins, outs;
    for (const auto& [nid, inner] : node->subgraph->nodes()) {
        auto name_it = inner.params.find("name");
        auto type_it = inner.params.find("type");
        PortSpec spec;
        spec.name = name_it != inner.params.end() ? name_it->second.AsString() : "port" + std::to_string(nid);
        spec.type = type_it != inner.params.end() ? ValueTypeFromName(type_it->second.AsString()) : ValueType::kAny;
        if (inner.type == "graph.input") ins.emplace_back(inner.y, spec);
        if (inner.type == "graph.output") outs.emplace_back(inner.y, spec);
    }
    auto by_y = [](const auto& a, const auto& b) { return a.first < b.first; };
    std::stable_sort(ins.begin(), ins.end(), by_y);
    std::stable_sort(outs.begin(), outs.end(), by_y);
    node->inputs.clear();
    node->outputs.clear();
    for (auto& p : ins) node->inputs.push_back(p.second);
    for (auto& p : outs) node->outputs.push_back(p.second);
    // Drop links that point at ports which no longer exist.
    for (auto it = links_.begin(); it != links_.end();) {
        const bool stale = (it->second.to.node == id && !node->FindInput(it->second.to.port)) ||
                           (it->second.from.node == id && !node->FindOutput(it->second.from.port));
        it = stale ? links_.erase(it) : std::next(it);
    }
    MarkDirty(id);
    Touch();
    return true;
}
 
bool Graph::CanConnect(const PortRef& from, const PortRef& to, std::string* reason) const {
    auto fail = [&](const std::string& msg) {
        if (reason) *reason = msg;
        return false;
    };
    const Node* src = FindNode(from.node);
    const Node* dst = FindNode(to.node);
    if (!src || !dst) return fail("node not found");
    if (from.node == to.node) return fail("cannot connect a node to itself");
    const PortSpec* out = src->FindOutput(from.port);
    const PortSpec* in = dst->FindInput(to.port);
    if (!out) return fail("output port not found: " + from.port);
    if (!in) return fail("input port not found: " + to.port);
    if (!AreTypesCompatible(out->type, in->type)) {
        return fail(std::string("type mismatch: ") + ValueTypeName(out->type) + " -> " + ValueTypeName(in->type));
    }
    if (WouldCreateCycle(from.node, to.node)) return fail("connection would create a cycle");
    return true;
}
 
LinkId Graph::Connect(const PortRef& from, const PortRef& to, std::string* error_out,
                      std::optional<Link>* replaced_out) {
    std::optional<Link> existing;
    if (const Link* current = FindLinkTo(to)) existing = *current;
    if (!CanConnect(from, to, error_out)) return kInvalidLink;

    if (existing) links_.erase(existing->id);
    Link link{next_link_id_++, from, to};
    links_.emplace(link.id, link);
    if (replaced_out) *replaced_out = std::move(existing);
    MarkDirty(to.node);
    Touch();
    return link.id;
}
 
bool Graph::InsertLink(const Link& link, std::string* error_out) {
    if (links_.count(link.id)) {
        if (error_out) *error_out = "duplicate link id";
        return false;
    }
    std::optional<Link> existing;
    if (const Link* current = FindLinkTo(link.to)) existing = *current;
    if (!CanConnect(link.from, link.to, error_out)) return false;

    if (existing) links_.erase(existing->id);
    links_.emplace(link.id, link);
    next_link_id_ = std::max(next_link_id_, link.id + 1);
    MarkDirty(link.to.node);
    Touch();
    return true;
}
 
bool Graph::Disconnect(LinkId id) {
    auto it = links_.find(id);
    if (it == links_.end()) return false;
    const NodeId target = it->second.to.node;
    links_.erase(it);
    MarkDirty(target);
    Touch();
    return true;
}
 
const Link* Graph::FindLink(LinkId id) const {
    auto it = links_.find(id);
    return it == links_.end() ? nullptr : &it->second;
}
 
const Link* Graph::FindLinkTo(const PortRef& input) const {
    for (const auto& [id, link] : links_) {
        if (link.to == input) return &link;
    }
    return nullptr;
}
 
std::vector<Link> Graph::LinksOf(NodeId id) const {
    std::vector<Link> out;
    for (const auto& [lid, link] : links_) {
        if (link.from.node == id || link.to.node == id) out.push_back(link);
    }
    return out;
}
 
uint64_t Graph::AddGroup(Group group) {
    if (group.id == 0) group.id = next_group_id_;
    next_group_id_ = std::max(next_group_id_, group.id + 1);
    const uint64_t id = group.id;
    groups_[id] = std::move(group);
    Touch();
    return id;
}
 
bool Graph::RemoveGroup(uint64_t id) {
    const bool removed = groups_.erase(id) != 0;
    if (removed) Touch();
    return removed;
}
 
bool Graph::WouldCreateCycle(NodeId from, NodeId to) const {
    // Adding from->to creates a cycle iff `from` is reachable from `to`.
    if (from == to) return true;
    return Downstream(to).count(from) != 0;
}
 
std::set<NodeId> Graph::Downstream(NodeId id) const {
    std::set<NodeId> seen;
    std::deque<NodeId> queue{id};
    while (!queue.empty()) {
        const NodeId cur = queue.front();
        queue.pop_front();
        for (const auto& [lid, link] : links_) {
            if (link.from.node == cur && seen.insert(link.to.node).second) queue.push_back(link.to.node);
        }
    }
    return seen;
}
 
std::set<NodeId> Graph::Upstream(NodeId id) const {
    std::set<NodeId> seen;
    std::deque<NodeId> queue{id};
    while (!queue.empty()) {
        const NodeId cur = queue.front();
        queue.pop_front();
        for (const auto& [lid, link] : links_) {
            if (link.to.node == cur && seen.insert(link.from.node).second) queue.push_back(link.from.node);
        }
    }
    return seen;
}
 
std::vector<std::vector<NodeId>> Graph::ExecutionLevels() const {
    std::map<NodeId, int> indegree;
    std::map<NodeId, std::vector<NodeId>> edges;
    for (const auto& [id, node] : nodes_) indegree[id] = 0;
    for (const auto& [lid, link] : links_) {
        edges[link.from.node].push_back(link.to.node);
        ++indegree[link.to.node];
    }
    std::vector<std::vector<NodeId>> levels;
    std::vector<NodeId> current;
    for (const auto& [id, deg] : indegree) if (deg == 0) current.push_back(id);
    while (!current.empty()) {
        levels.push_back(current);
        std::vector<NodeId> next;
        for (NodeId n : current) {
            for (NodeId m : edges[n]) {
                if (--indegree[m] == 0) next.push_back(m);
            }
        }
        std::sort(next.begin(), next.end());
        current = std::move(next);
    }
    return levels;
}
 
std::vector<NodeId> Graph::TopologicalOrder(std::string* error_out) const {
    std::vector<NodeId> order;
    for (const auto& level : ExecutionLevels()) order.insert(order.end(), level.begin(), level.end());
    if (order.size() != nodes_.size()) {
        if (error_out) *error_out = "graph contains a cycle";
        return {};
    }
    return order;
}
 
std::vector<ValidationIssue> Graph::Validate() const {
    std::vector<ValidationIssue> issues;
    std::string cycle_error;
    if (TopologicalOrder(&cycle_error).empty() && !nodes_.empty()) {
        issues.push_back({ValidationIssue::Severity::kError, kInvalidNode, cycle_error});
    }
    for (const auto& [id, node] : nodes_) {
        if (!registry_->Find(node.type)) {
            issues.push_back({ValidationIssue::Severity::kError, id, "unknown node type: " + node.type});
            continue;
        }
        for (const auto& in : node.inputs) {
            if (in.default_value.IsNone() && !FindLinkTo({id, in.name})) {
                issues.push_back({ValidationIssue::Severity::kWarning, id,
                                  "input '" + in.name + "' of '" + node.title + "' is not connected"});
            }
        }
    }
    for (const auto& [lid, link] : links_) {
        const Node* src = FindNode(link.from.node);
        const Node* dst = FindNode(link.to.node);
        if (!src || !dst || !src->FindOutput(link.from.port) || !dst->FindInput(link.to.port)) {
            issues.push_back({ValidationIssue::Severity::kError, link.to.node,
                              "dangling link " + std::to_string(lid)});
        }
    }
    return issues;
}
 
void Graph::MarkDirty(NodeId id) {
    if (!nodes_.count(id)) return;
    dirty_.insert(id);
    for (NodeId n : Downstream(id)) dirty_.insert(n);
}
 
void Graph::MarkAllDirty() {
    for (const auto& [id, node] : nodes_) dirty_.insert(id);
}
 
void Graph::Clear() {
    nodes_.clear();
    links_.clear();
    groups_.clear();
    dirty_.clear();
    next_node_id_ = next_link_id_ = next_group_id_ = 1;
    Touch();
}
 
}  // namespace XER::NodeGraph