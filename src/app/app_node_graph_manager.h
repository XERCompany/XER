#pragma once

#include <string>
#include "node_graph/node_graph.h"
#include "node_graph/node_graph_executor.h"
#include "node_graph/node_graph_registry.h"
#include "node_graph/node_graph_serializer.h"

namespace App {

class NodeGraphManager {
public:
    NodeGraphManager();
    ~NodeGraphManager();

    // Initialize node graph system
    bool Initialize();
    bool IsInitialized() const { return initialized_; }

    // Register audio input node type
    bool RegisterAudioInputNode(const std::string& audio_path);

    // Create and configure graph
    bool CreateGraph();
    XER::NodeGraph::Graph& GetGraph() { return graph_; }

    // Execute graph
    bool ExecuteGraph();
    XER::NodeGraph::ExecutionResult GetLastResult() const { return last_result_; }

    // Serialize/Deserialize
    std::string SerializeGraph() const;
    bool DeserializeGraph(const std::string& json, std::string* error_out = nullptr);

    // Graph info
    size_t GetNodeCount() const { return graph_.nodes().size(); }
    size_t GetLinkCount() const { return graph_.links().size(); }

private:
    bool initialized_;
    XER::NodeGraph::Graph graph_;
    XER::NodeGraph::ExecutionResult last_result_;
    std::string audio_path_;
};

}  // namespace App
