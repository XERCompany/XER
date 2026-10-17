#include "node_graph/node_graph_executor.h"
 
#include <algorithm>
#include <chrono>
#include <exception>
#include <set>
#include <thread>
 
namespace XER::NodeGraph {
 
Json ExecutionResult::ToJson(const Executor& executor) const {
    Json out = Json::MakeObject();
    out["ok"] = ok;
    out["error"] = error;
    out["evaluated"] = static_cast<uint64_t>(evaluated);
    out["cached"] = static_cast<uint64_t>(cached);
    out["levels"] = static_cast<uint64_t>(levels);
    out["elapsedMs"] = elapsed_ms;
    Json errors = Json::MakeObject();
    for (const auto& [id, msg] : node_errors) errors[std::to_string(id)] = msg;
    out["nodeErrors"] = errors;
    Json logs_json = Json::MakeArray();
    for (const auto& l : logs) logs_json.Push(l);
    out["logs"] = logs_json;
    Json outputs = Json::MakeObject();
    for (const auto& [name, value] : executor.external_outputs()) outputs[name] = value.ToJson();
    out["outputs"] = outputs;
    return out;
}
 
Executor::Executor(Graph& graph) : Executor(graph, Options{}) {}
 
Executor::Executor(Graph& graph, Options options) : graph_(graph), options_(options) {}
 
void Executor::SetExternalInput(const std::string& name, Value value) {
    external_inputs_[name] = std::move(value);
}
 
Value Executor::ExternalInput(const std::string& name) const {
    auto it = external_inputs_.find(name);
    return it == external_inputs_.end() ? Value() : it->second;
}
 
Value Executor::GetOutput(NodeId node, const std::string& port) const {
    auto it = cache_.find(node);
    if (it == cache_.end()) return Value();
    auto pit = it->second.find(port);
    return pit == it->second.end() ? Value() : pit->second;
}
 
const std::map<std::string, Value>* Executor::GetOutputs(NodeId node) const {
    auto it = cache_.find(node);
    return it == cache_.end() ? nullptr : &it->second;
}
 
void Executor::InvalidateCache() {
    cache_.clear();
    graph_.MarkAllDirty();
}
 
EvalContext Executor::PrepareContext(const Node& node) {
    EvalContext ctx;
    ctx.node = &node;
    ctx.executor = this;
    for (const auto& in : node.inputs) {
        // Unconnected inputs may be overridden inline through a param of the same name.
        auto override_it = node.params.find(in.name);
        Value v = override_it != node.params.end() ? override_it->second : in.default_value;
        if (const Link* link = graph_.FindLinkTo({node.id, in.name})) {
            v = GetOutput(link->from.node, link->from.port);
        }
        ctx.inputs[in.name] = v.IsNone() ? v : v.ConvertTo(in.type);
    }
    return ctx;
}
 
bool Executor::EvaluateNode(NodeId id, EvalContext& ctx) const {
    const NodeTypeDescriptor* desc = graph_.registry().Find(ctx.node->type);
    if (!desc || !desc->evaluate) {
        ctx.error = "no evaluator for node type " + ctx.node->type;
        return false;
    }
    try {
        if (!desc->evaluate(ctx)) {
            if (ctx.error.empty()) ctx.error = "evaluation failed";
            return false;
        }
    } catch (const std::exception& e) {
        ctx.error = std::string("exception: ") + e.what();
        return false;
    }
    for (const auto& out : ctx.node->outputs) {
        auto it = ctx.outputs.find(out.name);
        if (it != ctx.outputs.end() && !it->second.IsNone()) it->second = it->second.ConvertTo(out.type);
    }
    (void)id;
    return true;
}
 
ExecutionResult Executor::Run() {
    const auto start = std::chrono::steady_clock::now();
    ExecutionResult result;
    external_outputs_.clear();
 
    std::string cycle_error;
    if (graph_.TopologicalOrder(&cycle_error).empty() && !graph_.nodes().empty()) {
        result.ok = false;
        result.error = cycle_error;
        return result;
    }
 
    // Remove cache entries of deleted nodes.
    for (auto it = cache_.begin(); it != cache_.end();) {
        it = graph_.FindNode(it->first) ? std::next(it) : cache_.erase(it);
    }
 
    const auto levels = graph_.ExecutionLevels();
    result.levels = levels.size();
    std::set<NodeId> changed;
    std::set<NodeId> failed;
    size_t threads = options_.max_threads ? options_.max_threads : std::max(1u, std::thread::hardware_concurrency());
 
    for (const auto& level : levels) {
        std::vector<NodeId> work;
        for (NodeId id : level) {
            const Node* node = graph_.FindNode(id);
            const NodeTypeDescriptor* desc = graph_.registry().Find(node->type);
            bool upstream_failed = false;
            bool needs = !options_.incremental || graph_.IsDirty(id) || !cache_.count(id) || !desc || !desc->pure;
            for (const auto& link : graph_.LinksOf(id)) {
                if (link.to.node != id) continue;
                if (changed.count(link.from.node)) needs = true;
                if (failed.count(link.from.node)) upstream_failed = true;
            }
            if (upstream_failed) {
                failed.insert(id);
                result.node_errors[id] = "skipped: upstream node failed";
                cache_.erase(id);
                continue;
            }
            if (needs) work.push_back(id);
            else ++result.cached;
        }
 
        std::vector<EvalContext> contexts;
        contexts.reserve(work.size());
        for (NodeId id : work) contexts.push_back(PrepareContext(*graph_.FindNode(id)));
        std::vector<char> ok(work.size(), 0);
 
        auto eval_range = [&](size_t begin, size_t end) {
            for (size_t i = begin; i < end; ++i) ok[i] = EvaluateNode(work[i], contexts[i]) ? 1 : 0;
        };
        const size_t workers = options_.parallel ? std::min(threads, work.size()) : 1;
        if (workers <= 1) {
            eval_range(0, work.size());
        } else {
            std::vector<std::thread> pool;
            const size_t chunk = (work.size() + workers - 1) / workers;
            for (size_t w = 0; w < workers; ++w) {
                const size_t b = w * chunk;
                const size_t e = std::min(work.size(), b + chunk);
                if (b < e) pool.emplace_back(eval_range, b, e);
            }
            for (auto& t : pool) t.join();
        }
 
        for (size_t i = 0; i < work.size(); ++i) {
            const NodeId id = work[i];
            auto& ctx = contexts[i];
            for (auto& l : ctx.logs) result.logs.push_back(std::move(l));
            ++result.evaluated;
            if (!ok[i]) {
                failed.insert(id);
                result.node_errors[id] = ctx.error;
                cache_.erase(id);
                continue;
            }
            auto it = cache_.find(id);
            if (it == cache_.end() || it->second != ctx.outputs) changed.insert(id);
            cache_[id] = std::move(ctx.outputs);
            graph_.ClearDirty(id);
        }
    }
 
    for (const auto& [id, node] : graph_.nodes()) {
        if (node.type != "graph.output") continue;
        auto name_it = node.params.find("name");
        const std::string name = name_it != node.params.end() ? name_it->second.AsString() : "out";
        external_outputs_[name] = GetOutput(id, "value");
    }
 
    if (!result.node_errors.empty()) {
        result.ok = false;
        if (result.error.empty()) result.error = std::to_string(result.node_errors.size()) + " node(s) failed";
    }
    result.elapsed_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    return result;
}
 
}  // namespace XER::NodeGraph