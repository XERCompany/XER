#pragma once
 
#include "node_graph/node_graph_registry.h"
#include "node_graph/node_graph_value.h"
 
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
 
namespace XER::NodeGraph {
 
using NodeId = uint64_t;
using LinkId = uint64_t;
constexpr NodeId kInvalidNode = 0;
constexpr LinkId kInvalidLink = 0;
 
struct Node {
    NodeId id = kInvalidNode;
    std::string type;
    std::string title;
    double x = 0.0;
    double y = 0.0;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::map<std::string, Value> params;
    // Nested graph for "graph.subgraph" nodes.
    std::shared_ptr<Graph> subgraph;
 
    const PortSpec* FindInput(const std::string& name) const;
    const PortSpec* FindOutput(const std::string& name) const;
};
 
struct PortRef {
    NodeId node = kInvalidNode;
    std::string port;
    bool operator==(const PortRef& o) const { return node == o.node && port == o.port; }
};
 
struct Link {
    LinkId id = kInvalidLink;
    PortRef from;  // output port
    PortRef to;    // input port
};
 
struct Group {
    uint64_t id = 0;
    std::string title;
    double x = 0, y = 0, w = 200, h = 120;
    std::string color = "#3b4252";
};
 
struct ValidationIssue {
    enum class Severity { kWarning, kError } severity = Severity::kWarning;
    NodeId node = kInvalidNode;
    std::string message;
};
 
// Directed acyclic data-flow graph. Not thread-safe: mutate from one thread and
// let Executor parallelize evaluation internally.
class Graph {
public:
    explicit Graph(NodeTypeRegistry* registry = &NodeTypeRegistry::Shared());
    Graph(const Graph& other);
    Graph& operator=(const Graph& other);
 
    NodeTypeRegistry& registry() const { return *registry_; }
    std::string name;
 
    // ── Nodes ─────────────────────────────────────────────
    NodeId AddNode(const std::string& type, double x = 0, double y = 0, std::string* error_out = nullptr);
    // Inserts a fully specified node (used by undo/deserialization). Fails on id clash.
    bool InsertNode(Node node, std::string* error_out = nullptr);
    bool RemoveNode(NodeId id);
    Node* FindNode(NodeId id);
    const Node* FindNode(NodeId id) const;
    const std::map<NodeId, Node>& nodes() const { return nodes_; }
    bool MoveNode(NodeId id, double x, double y);
    bool SetParam(NodeId id, const std::string& name, Value value);
    bool SetTitle(NodeId id, const std::string& title);
    // Re-derives the ports of a subgraph node from its graph.input/graph.output nodes.
    bool RefreshSubgraphPorts(NodeId id);
 
    // ── Links ─────────────────────────────────────────────
    // Connects an output to an input. An input accepts a single link: an existing link
    // is replaced (its id is returned through replaced_out). Rejects cycles and
    // incompatible types.
    LinkId Connect(const PortRef& from, const PortRef& to, std::string* error_out = nullptr,
                   std::optional<Link>* replaced_out = nullptr);
    bool InsertLink(const Link& link, std::string* error_out = nullptr);
    bool Disconnect(LinkId id);
    const Link* FindLink(LinkId id) const;
    const Link* FindLinkTo(const PortRef& input) const;
    std::vector<Link> LinksOf(NodeId id) const;
    const std::map<LinkId, Link>& links() const { return links_; }
    bool CanConnect(const PortRef& from, const PortRef& to, std::string* reason_out = nullptr) const;
 
    // ── Groups (visual frames) ────────────────────────────
    uint64_t AddGroup(Group group);
    bool RemoveGroup(uint64_t id);
    const std::map<uint64_t, Group>& groups() const { return groups_; }
 
    // ── Topology ──────────────────────────────────────────
    bool WouldCreateCycle(NodeId from, NodeId to) const;
    std::vector<NodeId> TopologicalOrder(std::string* error_out = nullptr) const;
    // Nodes grouped by dependency depth; nodes in one level are independent.
    std::vector<std::vector<NodeId>> ExecutionLevels() const;
    std::set<NodeId> Upstream(NodeId id) const;
    std::set<NodeId> Downstream(NodeId id) const;
    std::vector<ValidationIssue> Validate() const;
 
    // ── Change tracking ───────────────────────────────────
    uint64_t revision() const { return revision_; }
    void MarkDirty(NodeId id);  // marks node and everything downstream
    void MarkAllDirty();
    bool IsDirty(NodeId id) const { return dirty_.count(id) != 0; }
    void ClearDirty(NodeId id) { dirty_.erase(id); }
    const std::set<NodeId>& dirty() const { return dirty_; }
 
    NodeId next_node_id() const { return next_node_id_; }
    LinkId next_link_id() const { return next_link_id_; }
 
    void Clear();
 
private:
    void Touch() { ++revision_; }
 
    NodeTypeRegistry* registry_;
    std::map<NodeId, Node> nodes_;
    std::map<LinkId, Link> links_;
    std::map<uint64_t, Group> groups_;
    std::set<NodeId> dirty_;
    NodeId next_node_id_ = 1;
    LinkId next_link_id_ = 1;
    uint64_t next_group_id_ = 1;
    uint64_t revision_ = 0;
};
 
}  // namespace XER::NodeGraph