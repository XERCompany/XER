#include "onnx_node_map.h"

namespace XER::ModelsBuilder::Reader::Onnx {

OnnxNodeMap& OnnxNodeMap::GetInstance() {
	static OnnxNodeMap instance;
	return instance;
}

OnnxNodeMap::OnnxNodeMap() {
	mapping_ = {
			{"Gemm", {Core::LayerType::Dense, 128U, Core::ActivationType::Linear}},
			{"MatMul", {Core::LayerType::Dense, 128U, Core::ActivationType::Linear}},
			{"Conv", {Core::LayerType::Dense, 64U, Core::ActivationType::Linear}},
			{"ConvRelu", {Core::LayerType::Dense, 64U, Core::ActivationType::ReLU}},
			{"Relu", {Core::LayerType::Activation, 0U, Core::ActivationType::ReLU}},
			{"Sigmoid", {Core::LayerType::Activation, 0U, Core::ActivationType::Sigmoid}},
			{"Tanh", {Core::LayerType::Activation, 0U, Core::ActivationType::Tanh}},
			{"Softmax", {Core::LayerType::Activation, 0U, Core::ActivationType::SoftMax}},
			{"BatchNormalization", {Core::LayerType::BatchNorm, 64U, std::nullopt}},
	};
}

bool OnnxNodeMap::HasOp(const std::string& op_type) const {
	return mapping_.find(op_type) != mapping_.end();
}

NodeMapping OnnxNodeMap::Resolve(const OnnxNode& node) const {
	const auto it = mapping_.find(node.op_type);
	if (it != mapping_.end()) {
		return it->second;
	}
	return NodeMapping{Core::LayerType::Dense, 32U, Core::ActivationType::Linear};
}

}  // namespace XER::ModelsBuilder::Reader::Onnx

