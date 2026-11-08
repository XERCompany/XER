#include "onnx_graph_parser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace XER::ModelsBuilder::Reader::Onnx {

namespace {

bool ContainsToken(const std::string_view haystack, const std::string_view needle) {
	return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

bool OnnxGraphParser::ParseBuffer(const uint8_t* buffer,
																	size_t size,
																	ParsedOnnxGraph& graph) {
	graph = ParsedOnnxGraph{};
	if (buffer == nullptr || size == 0) {
		return false;
	}

	const std::string_view bytes(reinterpret_cast<const char*>(buffer), size);

	struct OpSignature {
		std::string_view token;
		std::string_view op;
	};

	static constexpr std::array<OpSignature, 10> kKnownOps = {{
			{"Conv", "Conv"},
			{"Gemm", "Gemm"},
			{"MatMul", "MatMul"},
			{"Relu", "Relu"},
			{"Sigmoid", "Sigmoid"},
			{"Tanh", "Tanh"},
			{"Softmax", "Softmax"},
			{"BatchNormalization", "BatchNormalization"},
			{"MaxPool", "MaxPool"},
			{"AveragePool", "AveragePool"},
	}};

	size_t op_index = 0;
	for (const auto& sig : kKnownOps) {
		if (!ContainsToken(bytes, sig.token)) {
			continue;
		}

		OnnxNode node;
		node.op_type = std::string(sig.op);
		node.name = std::string("node_") + std::to_string(op_index) + "_" + std::string(sig.op);
		node.inputs = {op_index == 0 ? "input_0" : "tensor_" + std::to_string(op_index - 1)};
		node.outputs = {"tensor_" + std::to_string(op_index)};
		graph.nodes.push_back(std::move(node));
		++op_index;
	}

	if (graph.nodes.empty()) {
		OnnxNode passthrough;
		passthrough.op_type = "Identity";
		passthrough.name = "node_0_identity";
		passthrough.inputs = {"input_0"};
		passthrough.outputs = {"tensor_0"};
		graph.nodes.push_back(std::move(passthrough));
	}

	graph.outputs.push_back(graph.nodes.back().outputs.front());
	graph.metadata = ExtractMetadata(buffer, size);
	graph.metadata["nodes"] = std::to_string(graph.nodes.size());
	return true;
}

std::map<std::string, std::string> OnnxGraphParser::ExtractMetadata(const uint8_t* buffer,
																																		size_t size) {
	std::map<std::string, std::string> meta;
	if (buffer == nullptr || size == 0) {
		return meta;
	}

	const std::string_view bytes(reinterpret_cast<const char*>(buffer), size);
	meta["bytes"] = std::to_string(size);
	meta["contains_onnx"] = ContainsToken(bytes, "onnx") ? "true" : "false";
	meta["producer_pytorch"] = ContainsToken(bytes, "pytorch") ? "true" : "false";
	meta["producer_tensorflow"] = ContainsToken(bytes, "tensorflow") ? "true" : "false";
	meta["producer_onnxruntime"] = ContainsToken(bytes, "onnxruntime") ? "true" : "false";
	return meta;
}

}  // namespace XER::ModelsBuilder::Reader::Onnx

