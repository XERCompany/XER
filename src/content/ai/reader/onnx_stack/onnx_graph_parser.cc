#include "onnx_graph_parser.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

// XER_AI_HAS_ONNX_PROTO is defined by src/content/ai/CMakeLists.txt when
// ONNX::onnx_proto / ONNX::onnx is actually linked — it provides the
// protobuf-generated model definitions (<onnx/onnx_pb.h>). Note this is
// deliberately distinct from XER_AI_HAS_ONNX (the onnxruntime inference
// engine): protobuf availability makes this a real parser, not a
// runtime.
#if defined(XER_AI_HAS_ONNX_PROTO)
#include <onnx/onnx_pb.h>
#endif

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

#if defined(XER_AI_HAS_ONNX_PROTO)
	// Real parse path: ONNX::onnx_proto is linked, so decode the actual
	// ModelProto instead of guessing at op names from a byte scan.
	onnx::ModelProto model;
	if (!model.ParseFromArray(buffer, static_cast<int>(size))) {
		return false;
	}

	const onnx::GraphProto& onnx_graph = model.graph();
	size_t anon_index = 0;
	for (const onnx::NodeProto& proto_node : onnx_graph.node()) {
		OnnxNode node;
		node.op_type = proto_node.op_type();
		node.name = proto_node.name().empty()
			? ("node_" + std::to_string(anon_index++) + "_" + node.op_type)
			: proto_node.name();
		node.inputs.assign(proto_node.input().begin(), proto_node.input().end());
		node.outputs.assign(proto_node.output().begin(), proto_node.output().end());
		graph.nodes.push_back(std::move(node));
	}

	for (const onnx::ValueInfoProto& output : onnx_graph.output()) {
		graph.outputs.push_back(output.name());
	}
	if (graph.outputs.empty() && !graph.nodes.empty() &&
			!graph.nodes.back().outputs.empty()) {
		graph.outputs.push_back(graph.nodes.back().outputs.front());
	}

	graph.metadata = ExtractMetadata(buffer, size);
	graph.metadata["nodes"] = std::to_string(graph.nodes.size());
	return true;
#else
	// Fallback WITHOUT the protobuf lib: a byte scan for known op-name
	// tokens. This can only guess at graph structure — build with
	// XER_AI_HAS_ONNX_PROTO for a real decode.
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
#endif
}

std::map<std::string, std::string> OnnxGraphParser::ExtractMetadata(const uint8_t* buffer,
																																		size_t size) {
	std::map<std::string, std::string> meta;
	if (buffer == nullptr || size == 0) {
		return meta;
	}

	const std::string_view bytes(reinterpret_cast<const char*>(buffer), size);
	meta["bytes"] = std::to_string(size);

#if defined(XER_AI_HAS_ONNX_PROTO)
	// Real metadata from the decoded ModelProto.
	onnx::ModelProto model;
	if (model.ParseFromArray(buffer, static_cast<int>(size))) {
		meta["ir_version"] = std::to_string(model.ir_version());
		meta["producer_name"] = model.producer_name();
		meta["producer_version"] = model.producer_version();
		meta["graph_name"] = model.graph().name();
		meta["nodes"] = std::to_string(model.graph().node_size());
		meta["contains_onnx"] = "true";
		return meta;
	}
	meta["parse_error"] = "true";
#endif

	meta["contains_onnx"] = ContainsToken(bytes, "onnx") ? "true" : "false";
	meta["producer_pytorch"] = ContainsToken(bytes, "pytorch") ? "true" : "false";
	meta["producer_tensorflow"] = ContainsToken(bytes, "tensorflow") ? "true" : "false";
	meta["producer_onnxruntime"] = ContainsToken(bytes, "onnxruntime") ? "true" : "false";
	return meta;
}

}  // namespace XER::ModelsBuilder::Reader::Onnx
