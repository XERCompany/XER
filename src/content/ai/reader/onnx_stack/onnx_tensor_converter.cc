#include "onnx_tensor_converter.h"

#include <algorithm>
#include <cstring>

#if __has_include(<fp16.h>)
#include <fp16.h>
#define XER_AI_HAS_FP16_HEADER 1
#else
#define XER_AI_HAS_FP16_HEADER 0
#endif

namespace XER::ModelsBuilder::Reader::Onnx {

std::vector<float> OnnxTensorConverter::ToFloatVector(const uint8_t* data, size_t bytes) {
	if (data == nullptr || bytes < sizeof(float)) {
		return {};
	}

	const size_t count = bytes / sizeof(float);
	std::vector<float> out(count, 0.0f);
	std::memcpy(out.data(), data, count * sizeof(float));
	return out;
}

std::vector<float> OnnxTensorConverter::FloatToFp16Aware(const std::vector<float>& input) {
	if (input.empty()) {
		return {};
	}

#if XER_AI_HAS_FP16_HEADER
	std::vector<float> out(input.size(), 0.0f);
	for (size_t i = 0; i < input.size(); ++i) {
		const uint16_t packed = fp16_ieee_from_fp32_value(input[i]);
		out[i] = fp16_ieee_to_fp32_value(packed);
	}
	return out;
#else
	return input;
#endif
}

}  // namespace XER::ModelsBuilder::Reader::Onnx

