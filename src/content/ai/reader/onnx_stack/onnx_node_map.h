#pragma once

#include "onnx_optimizer.h"

#include "../../models_builder/model_core/layer.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace XER::ModelsBuilder::Reader::Onnx {

struct NodeMapping {
	Core::LayerType layer_type;
	uint32_t default_units;
	std::optional<Core::ActivationType> activation;
};

class OnnxNodeMap {
 public:
	static OnnxNodeMap& GetInstance();

	bool HasOp(const std::string& op_type) const;
	NodeMapping Resolve(const OnnxNode& node) const;

 private:
	OnnxNodeMap();

	std::unordered_map<std::string, NodeMapping> mapping_;
};

}  // namespace XER::ModelsBuilder::Reader::Onnx

