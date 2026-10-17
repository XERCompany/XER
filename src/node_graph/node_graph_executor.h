#pragma once
 
#include "node_graph/node_graph.h"
 
#include <map>
#include <string>
#include <vector>
 
namespace XER::NodeGraph {
 
struct ExecutionResult {
    bool ok = true;
    std::string error;
    std::map<NodeId, std::string> node_errors;
    std::vector<std::string> logs;
    size_t evaluated = 0;
    size_t cached = 0;
    size_t levels = 0;
    double elapsed_ms = 0.0;
 
    Json ToJson(const class Executor& executor) const;
};
 
// Evaluates a Graph level by level. Nodes in the same level have no mutual
// dependencies and are evaluated concurrently. Pure nodes keep their cached
// outputs until they (or something upstream) become dirty.
class Executor {
public:
    struct Options {
        bool parallel = true;
        size_t max_threads = 0;  // 0 = hardware concurrency
        bool incremental = true;
    };
 
    explicit Executor(Graph& graph);
    Executor(Graph& graph, Options options);
 
    ExecutionResult Run();
 
    // Values fed to graph.input nodes (by their "name" parameter) when the graph is
    // used as a subgraph or called from script.
    void SetExternalInput(const std::string& name, Value value);
    Value ExternalInput(const std::string& name) const;
    // Values collected from graph.output nodes after Run().
    const std::map<std::string, Value>& external_outputs() const { return external_outputs_; }
 
    Value GetOutput(NodeId node, const std::string& port) const;
    const std::map<std::string, Value>* GetOutputs(NodeId node) const;
    void InvalidateCache();
 
private:
    bool EvaluateNode(NodeId id, EvalContext& ctx) const;
    EvalContext PrepareContext(const Node& node);
 
    Graph& graph_;
    Options options_;
    std::map<NodeId, std::map<std::string, Value>> cache_;
    std::map<std::string, Value> external_inputs_;
    std::map<std::string, Value> external_outputs_;
};
 
}  // namespace XER::NodeGraph