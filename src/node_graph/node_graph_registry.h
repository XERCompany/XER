#pragma once
 
#include "node_graph/node_graph_value.h"
 
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
 
namespace XER::NodeGraph {
 
class Graph;
class Executor;
struct Node;
 
struct PortSpec {
    std::string name;
    ValueType type = ValueType::kAny;
    Value default_value;
};
 
struct ParamSpec {
    std::string name;
    Value default_value;
    // Optional list of allowed string values (rendered as a dropdown by editors).
    std::vector<std::string> options;
};
 
struct EvalContext {
    const Node* node = nullptr;
    std::map<std::string, Value> inputs;
    std::map<std::string, Value> outputs;
    std::vector<std::string> logs;
    std::string error;
    Executor* executor = nullptr;
 
    Value Input(const std::string& name) const;
    Value Param(const std::string& name) const;
    void Output(const std::string& name, Value value) { outputs[name] = std::move(value); }
};
 
using EvaluateFn = std::function<bool(EvalContext&)>;
 
struct NodeTypeDescriptor {
    std::string type;       // e.g. "math.add"
    std::string title;      // display name
    std::string category;   // e.g. "Math"
    std::string description;
    std::vector<PortSpec> inputs;
    std::vector<PortSpec> outputs;
    std::vector<ParamSpec> params;
    EvaluateFn evaluate;
    // Pure nodes can be cached until an upstream change; impure nodes (random, time,
    // print) are re-evaluated on every run.
    bool pure = true;
};
 
class NodeTypeRegistry {
public:
    // Process-wide registry pre-populated with the built-in node library.
    static NodeTypeRegistry& Shared();
 
    NodeTypeRegistry();
 
    bool Register(NodeTypeDescriptor descriptor);
    bool Unregister(const std::string& type);
    const NodeTypeDescriptor* Find(const std::string& type) const;
    std::vector<std::string> Types() const;
    std::vector<std::string> Categories() const;
 
    // Machine-readable catalog for editors.
    Json ToJson() const;
 
private:
    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<const NodeTypeDescriptor>> types_;
};
 
void RegisterBuiltinNodes(NodeTypeRegistry& registry);
 
}  // namespace XER::NodeGraph