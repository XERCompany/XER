#include "onnx_loader.h"

#include "onnx_graph_parser.h"
#include "onnx_attribute_reader.h"
#include "onnx_node_map.h"
#include "onnx_optimizer.h"
#include "onnx_tensor_converter.h"

#include "../../models_builder/model_core/dense_layer.h"

#include "../utils/rm_cache_manager.h"
#include "../utils/rm_logger.h"

#include <algorithm>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace XER::ModelsBuilder::Reader::Onnx {

std::shared_ptr<Core::Model> OnnxLoader::Load(const std::string& filepath) {
    Utils::ReaderLogger::GetInstance().Info("Loading ONNX model from path: " + filepath);

    if (const auto cached = Utils::ReaderCacheManager::GetInstance().Get(filepath); cached.has_value()) {
        Utils::ReaderLogger::GetInstance().Debug("Using cached ONNX bytes for: " + filepath);
        return LoadFromBuffer(cached->data(), cached->size());
    }

    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open ONNX file: " + filepath);
    }

    file.seekg(0, std::ios::end);
    const std::streampos end = file.tellg();
    if (end <= 0) {
        throw std::runtime_error("ONNX file is empty: " + filepath);
    }

    const size_t file_size = static_cast<size_t>(end);
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size))) {
        throw std::runtime_error("Failed to read ONNX file: " + filepath);
    }

    Utils::ReaderCacheManager::GetInstance().Put(filepath, buffer);

    return LoadFromBuffer(buffer.data(), buffer.size());
}

std::shared_ptr<Core::Model> OnnxLoader::LoadFromBuffer(const uint8_t* buffer,
                                                         size_t size) {
    if (buffer == nullptr || size == 0U) {
        throw std::invalid_argument("Invalid buffer or size");
    }

    ParsedOnnxGraph graph;
    if (!OnnxGraphParser::ParseBuffer(buffer, size, graph)) {
        throw std::runtime_error("Failed to parse ONNX graph from buffer");
    }

    const auto attrs = OnnxAttributeReader::ReadCommonAttributes(buffer, size);
    const std::vector<float> raw_head = OnnxTensorConverter::ToFloatVector(
        buffer,
        std::min<size_t>(size, sizeof(float) * 32U));
    const std::vector<float> fp16_aware_head = OnnxTensorConverter::FloatToFp16Aware(raw_head);
    (void)fp16_aware_head;

    Utils::ReaderLogger::GetInstance().Info(
            "Parsed ONNX graph, nodes=" + std::to_string(graph.nodes.size()));

    std::vector<OnnxNode> optimized_nodes = graph.nodes;
    OnnxOptimizer::Optimize(optimized_nodes);

    auto model = std::make_shared<Core::Model>("OnnxModel");
    model->SetModelType(Core::ModelType::Sequential);

    const OnnxNodeMap& node_map = OnnxNodeMap::GetInstance();
    for (const OnnxNode& node : optimized_nodes) {
        if (node.op_type == "Identity") {
            continue;
        }

        const NodeMapping mapping = node_map.Resolve(node);
        uint32_t units = OnnxAttributeReader::ReadUnitsHint(attrs, mapping.default_units);
        if (units == 0U) {
            units = 32U;
        }

        auto layer = std::make_shared<::XER::ModelsBuilder::Core::DenseLayer>(units);
        layer->SetLayerName(node.name.empty() ? ("dense_" + node.op_type) : node.name);
        if (mapping.activation.has_value()) {
            layer->SetActivation(*mapping.activation);
        }
        model->AddLayer(layer);
    }

    if (model->GetLayerCount() == 0U) {
        model->AddLayer(std::make_shared<::XER::ModelsBuilder::Core::DenseLayer>(32U));
    }

    if (!model->Build({1U, 128U})) {
        throw std::runtime_error("Failed to build converted ONNX model");
    }

    if (!model->Compile()) {
        throw std::runtime_error("Failed to compile converted ONNX model");
    }

    Utils::ReaderLogger::GetInstance().Info("ONNX model converted and compiled successfully");

    return model;
}

std::string OnnxLoader::GetMetadata(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open ONNX file for metadata: " + filepath);
    }

    file.seekg(0, std::ios::end);
    const size_t file_size = static_cast<size_t>(file.tellg());
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(file_size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(file_size))) {
        throw std::runtime_error("Failed to read ONNX file for metadata: " + filepath);
    }

    const auto metadata = OnnxGraphParser::ExtractMetadata(buffer.data(), buffer.size());
    std::ostringstream ss;
    ss << "ONNX model: " << filepath;
    for (const auto& [key, value] : metadata) {
        ss << "\n- " << key << ": " << value;
    }
    return ss.str();
}

} // namespace XER::ModelsBuilder::Reader::Onnx
