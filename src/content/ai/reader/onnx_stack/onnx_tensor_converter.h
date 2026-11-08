#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::ModelsBuilder::Reader::Onnx {

class OnnxTensorConverter {
 public:
	static std::vector<float> ToFloatVector(const uint8_t* data, size_t bytes);
	static std::vector<float> FloatToFp16Aware(const std::vector<float>& input);
};

}  // namespace XER::ModelsBuilder::Reader::Onnx

