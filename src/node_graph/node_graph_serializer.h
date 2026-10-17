#pragma once
 
#include "node_graph/node_graph.h"
 
#include <string>
 
namespace XER::NodeGraph {
 
constexpr const char* kGraphFormat = "xer.nodegraph";
constexpr int kGraphFormatVersion = 1;
 
Json GraphToJson(const Graph& graph);
bool GraphFromJson(const Json& json, Graph* graph, std::string* error_out = nullptr);
 
std::string SerializeGraph(const Graph& graph, int indent = 2);
bool DeserializeGraph(const std::string& text, Graph* graph, std::string* error_out = nullptr);
 
bool SaveGraphToFile(const Graph& graph, const std::string& path, std::string* error_out = nullptr);
bool LoadGraphFromFile(const std::string& path, Graph* graph, std::string* error_out = nullptr);
 
Json NodeToJson(const Node& node);
 
}  // namespace XER::NodeGraph