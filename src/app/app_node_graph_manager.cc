#include "app/app_node_graph_manager.h"

namespace App {

NodeGraphManager::NodeGraphManager()
    : initialized_(false) {
}

NodeGraphManager::~NodeGraphManager() {
}

bool NodeGraphManager::Initialize() {
    // Node graph system is already initialized via registry
    initialized_ = true;
    return true;
}

bool NodeGraphManager::RegisterAudioInputNode(const std::string& audio_path) {
    audio_path_ = audio_path;
    
    auto& node_type_registry = XER::NodeGraph::NodeTypeRegistry::Shared();
    XER::NodeGraph::NodeTypeDescriptor descriptor_audio_input;
    descriptor_audio_input.type = "audio_input";
    descriptor_audio_input.inputs = { 
        XER::NodeGraph::PortSpec{"input", XER::NodeGraph::ValueType::kString}
    };
    descriptor_audio_input.title = "Audio Input";
    
    // Placeholder evaluation - would need proper context
    descriptor_audio_input.evaluate = [](XER::NodeGraph::EvalContext& ctx) -> bool {
        // Audio loading logic would go here
        return true;
    };
    
    node_type_registry.Register(descriptor_audio_input);
    return true;
}

bool NodeGraphManager::CreateGraph() {
    graph_ = XER::NodeGraph::Graph();
    
    std::string error;
    auto node_id = graph_.AddNode("audio_input", 0, 1, &error);
    if (node_id == XER::NodeGraph::kInvalidNode) {
        return false;
    }
    
    if (!graph_.SetParam(node_id, "input", XER::NodeGraph::Value(audio_path_))) {
        return false;
    }
    
    return true;
}

bool NodeGraphManager::ExecuteGraph() {
    XER::NodeGraph::Executor executor(graph_);
    last_result_ = executor.Run();
    return last_result_.ok;
}

std::string NodeGraphManager::SerializeGraph() const {
    return XER::NodeGraph::SerializeGraph(graph_);
}

bool NodeGraphManager::DeserializeGraph(const std::string& json, std::string* error_out) {
    return XER::NodeGraph::DeserializeGraph(json, &graph_, error_out);
}

}  // namespace App
