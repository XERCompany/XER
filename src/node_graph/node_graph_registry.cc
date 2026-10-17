#include "node_graph/node_graph_registry.h"
 
#include "node_graph/node_graph.h"
 
#include <set>
 
namespace XER::NodeGraph {
 
Value EvalContext::Input(const std::string& name) const {
    auto it = inputs.find(name);
    return it == inputs.end() ? Value() : it->second;
}
 
Value EvalContext::Param(const std::string& name) const {
    if (node == nullptr) return Value();
    auto it = node->params.find(name);
    return it == node->params.end() ? Value() : it->second;
}
 
NodeTypeRegistry& NodeTypeRegistry::Shared() {
    static NodeTypeRegistry* registry = [] {
        auto* r = new NodeTypeRegistry();
        RegisterBuiltinNodes(*r);
        return r;
    }();
    return *registry;
}
 
NodeTypeRegistry::NodeTypeRegistry() = default;
 
bool NodeTypeRegistry::Register(NodeTypeDescriptor descriptor) {
    if (descriptor.type.empty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string key = descriptor.type;
    types_[key] = std::make_shared<const NodeTypeDescriptor>(std::move(descriptor));
    return true;
}
 
bool NodeTypeRegistry::Unregister(const std::string& type) {
    std::lock_guard<std::mutex> lock(mutex_);
    return types_.erase(type) != 0;
}
 
const NodeTypeDescriptor* NodeTypeRegistry::Find(const std::string& type) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = types_.find(type);
    return it == types_.end() ? nullptr : it->second.get();
}
 
std::vector<std::string> NodeTypeRegistry::Types() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> out;
    out.reserve(types_.size());
    for (const auto& [k, v] : types_) out.push_back(k);
    return out;
}
 
std::vector<std::string> NodeTypeRegistry::Categories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> cats;
    for (const auto& [k, v] : types_) cats.insert(v->category);
    return {cats.begin(), cats.end()};
}
 
Json NodeTypeRegistry::ToJson() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Json out = Json::MakeArray();
    auto ports = [](const std::vector<PortSpec>& specs) {
        Json arr = Json::MakeArray();
        for (const auto& p : specs) {
            Json j = Json::MakeObject();
            j["name"] = p.name;
            j["type"] = ValueTypeName(p.type);
            if (!p.default_value.IsNone()) j["default"] = p.default_value.ToJson();
            arr.Push(std::move(j));
        }
        return arr;
    };
    for (const auto& [k, d] : types_) {
        Json j = Json::MakeObject();
        j["type"] = d->type;
        j["title"] = d->title;
        j["category"] = d->category;
        j["description"] = d->description;
        j["pure"] = d->pure;
        j["inputs"] = ports(d->inputs);
        j["outputs"] = ports(d->outputs);
        Json params = Json::MakeArray();
        for (const auto& p : d->params) {
            Json pj = Json::MakeObject();
            pj["name"] = p.name;
            pj["default"] = p.default_value.ToJson();
            if (!p.options.empty()) {
                Json opts = Json::MakeArray();
                for (const auto& o : p.options) opts.Push(o);
                pj["options"] = opts;
            }
            params.Push(std::move(pj));
        }
        j["params"] = params;
        out.Push(std::move(j));
    }
    return out;
}
 
}  // namespace XER::NodeGraph