#include "node_graph/node_graph_serializer.h"
 
#include <fstream>
#include <sstream>
 
namespace XER::NodeGraph {
 
namespace {
 
Json PortsToJson(const std::vector<PortSpec>& ports) {
    Json arr = Json::MakeArray();
    for (const auto& p : ports) {
        Json port = Json::MakeObject();
        port["name"] = p.name;
        port["type"] = ValueTypeName(p.type);
        if (!p.default_value.IsNone()) port["default"] = p.default_value.ToJson();
        arr.Push(std::move(port));
    }
    return arr;
}
 
Json PortRefToJson(const PortRef& ref) {
    Json j = Json::MakeObject();
    j["node"] = static_cast<uint64_t>(ref.node);
    j["port"] = ref.port;
    return j;
}
 
PortRef PortRefFromJson(const Json& j) {
    return PortRef{static_cast<NodeId>(j["node"].AsInt()), j["port"].AsString()};
}
 
}  // namespace
 
Json NodeToJson(const Node& node) {
    Json j = Json::MakeObject();
    j["id"] = static_cast<uint64_t>(node.id);
    j["type"] = node.type;
    j["title"] = node.title;
    j["x"] = node.x;
    j["y"] = node.y;
    Json params = Json::MakeObject();
    for (const auto& [k, v] : node.params) params[k] = v.ToJson();
    j["params"] = params;
    j["inputs"] = PortsToJson(node.inputs);
    j["outputs"] = PortsToJson(node.outputs);
    if (node.subgraph) j["subgraph"] = GraphToJson(*node.subgraph);
    return j;
}
 
Json GraphToJson(const Graph& graph) {
    Json j = Json::MakeObject();
    j["format"] = kGraphFormat;
    j["version"] = kGraphFormatVersion;
    j["name"] = graph.name;
    Json nodes = Json::MakeArray();
    for (const auto& [id, node] : graph.nodes()) nodes.Push(NodeToJson(node));
    j["nodes"] = nodes;
    Json links = Json::MakeArray();
    for (const auto& [id, link] : graph.links()) {
        Json l = Json::MakeObject();
        l["id"] = static_cast<uint64_t>(link.id);
        l["from"] = PortRefToJson(link.from);
        l["to"] = PortRefToJson(link.to);
        links.Push(std::move(l));
    }
    j["links"] = links;
    Json groups = Json::MakeArray();
    for (const auto& [id, g] : graph.groups()) {
        Json gj = Json::MakeObject();
        gj["id"] = static_cast<uint64_t>(g.id);
        gj["title"] = g.title;
        gj["x"] = g.x;
        gj["y"] = g.y;
        gj["w"] = g.w;
        gj["h"] = g.h;
        gj["color"] = g.color;
        groups.Push(std::move(gj));
    }
    j["groups"] = groups;
    return j;
}
 
bool GraphFromJson(const Json& json, Graph* graph, std::string* error_out) {
    auto fail = [&](const std::string& msg) {
        if (error_out) *error_out = msg;
        return false;
    };
    if (!json.IsObject()) return fail("graph document must be an object");
    if (json.Has("format") && json["format"].AsString() != kGraphFormat) {
        return fail("unsupported format: " + json["format"].AsString());
    }
    if (json["version"].AsInt(kGraphFormatVersion) > kGraphFormatVersion) {
        return fail("unsupported version " + std::to_string(json["version"].AsInt()));
    }
 
    Graph result(&graph->registry());
    result.name = json["name"].AsString();
    for (const auto& nj : json["nodes"].AsArray()) {
        const std::string type = nj["type"].AsString();
        const NodeTypeDescriptor* desc = graph->registry().Find(type);
        if (!desc) return fail("unknown node type: " + type);
        Node node;
        node.id = static_cast<NodeId>(nj["id"].AsInt());
        node.type = type;
        node.title = nj.Has("title") ? nj["title"].AsString() : desc->title;
        node.x = nj["x"].AsNumber();
        node.y = nj["y"].AsNumber();
        node.inputs = desc->inputs;
        node.outputs = desc->outputs;
        for (const auto& p : desc->params) node.params[p.name] = p.default_value;
        for (const auto& [k, v] : nj["params"].AsObject()) node.params[k] = Value::FromJson(v);
        if (type == "graph.subgraph") {
            node.subgraph = std::make_shared<Graph>(&graph->registry());
            if (nj.Has("subgraph")) {
                std::string sub_error;
                if (!GraphFromJson(nj["subgraph"], node.subgraph.get(), &sub_error)) {
                    return fail("subgraph of node " + std::to_string(node.id) + ": " + sub_error);
                }
            }
        }
        const NodeId id = node.id;
        std::string insert_error;
        if (!result.InsertNode(std::move(node), &insert_error)) return fail(insert_error);
        if (type == "graph.subgraph") result.RefreshSubgraphPorts(id);
    }
    for (const auto& lj : json["links"].AsArray()) {
        Link link{static_cast<LinkId>(lj["id"].AsInt()), PortRefFromJson(lj["from"]), PortRefFromJson(lj["to"])};
        if (link.id == kInvalidLink) link.id = result.next_link_id();
        std::string link_error;
        if (!result.InsertLink(link, &link_error)) {
            return fail("link " + std::to_string(link.id) + ": " + link_error);
        }
    }
    for (const auto& gj : json["groups"].AsArray()) {
        Group g;
        g.id = static_cast<uint64_t>(gj["id"].AsInt());
        g.title = gj["title"].AsString();
        g.x = gj["x"].AsNumber();
        g.y = gj["y"].AsNumber();
        g.w = gj["w"].AsNumber(200);
        g.h = gj["h"].AsNumber(120);
        if (gj.Has("color")) g.color = gj["color"].AsString();
        result.AddGroup(g);
    }
    result.MarkAllDirty();
    *graph = std::move(result);
    return true;
}
 
std::string SerializeGraph(const Graph& graph, int indent) { return GraphToJson(graph).Dump(indent); }
 
bool DeserializeGraph(const std::string& text, Graph* graph, std::string* error_out) {
    Json json;
    if (!Json::Parse(text, &json, error_out)) return false;
    return GraphFromJson(json, graph, error_out);
}
 
bool SaveGraphToFile(const Graph& graph, const std::string& path, std::string* error_out) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        if (error_out) *error_out = "cannot open " + path;
        return false;
    }
    out << SerializeGraph(graph);
    return static_cast<bool>(out);
}
 
bool LoadGraphFromFile(const std::string& path, Graph* graph, std::string* error_out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error_out) *error_out = "cannot open " + path;
        return false;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return DeserializeGraph(ss.str(), graph, error_out);
}
 
}  // namespace XER::NodeGraph