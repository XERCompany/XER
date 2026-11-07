#pragma once
 
#include "node_graph/node_graph.h"
 
#include <map>
#include <set>
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

    // Step-wise execution: each StepOnce() evaluates exactly one execution
    // level (nodes within a level are still parallel). First call starts a
    // session; finished==true once every level ran. Port values are
    // available via GetOutput/GetOutputs after each step.
    struct StepInfo {
        size_t level = 0;             // 0-based index of the level just run
        size_t total_levels = 0;
        std::vector<NodeId> nodes;    // nodes evaluated in this step
        bool finished = false;        // session complete
        bool started = false;         // a session is/was active
        std::string error;
    };
    bool StepOnce(StepInfo* info);
    void ResetStep();
    bool StepActive() const { return step_started_; }
    // Cumulative result of the active/finished step session.
    const ExecutionResult& step_result() const { return step_result_; }
 
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
    // Evaluates one level; appends logs/errors into result, updates
    // changed/failed, and records evaluated ids into evaluated_out.
    void EvalLevel(const std::vector<NodeId>& level, ExecutionResult& result,
                   std::set<NodeId>& changed, std::set<NodeId>& failed,
                   std::vector<NodeId>* evaluated_out);
    void FinalizeResult(ExecutionResult& result);
    void BeginStepSession();

    Graph& graph_;
    Options options_;
    std::map<NodeId, std::map<std::string, Value>> cache_;
    std::map<std::string, Value> external_inputs_;
    std::map<std::string, Value> external_outputs_;

    // Step-debug session state.
    bool step_started_ = false;
    size_t step_level_idx_ = 0;
    uint64_t step_revision_ = 0;
    std::vector<std::vector<NodeId>> step_levels_;
    std::set<NodeId> step_changed_;
    std::set<NodeId> step_failed_;
    ExecutionResult step_result_;
};
 
}  // namespace XER::NodeGraph