#pragma once

#include "onnx_optimizer.h"

#include <map>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Onnx {

struct ParsedOnnxGraph {
	std::vector<OnnxNode> nodes;
	std::vector<std::string> outputs;
	std::map<std::string, std::string> metadata;
};

class OnnxGraphParser {
 public:
	static bool ParseBuffer(const uint8_t* buffer, size_t size, ParsedOnnxGraph& graph);
	static std::map<std::string, std::string> ExtractMetadata(const uint8_t* buffer, size_t size);
};

}  // namespace XER::ModelsBuilder::Reader::Onnx

