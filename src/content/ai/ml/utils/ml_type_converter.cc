#include "ml_type_converter.h"

#include <algorithm>
#include <cstring>

#if __has_include(<fp16.h>)
#include <fp16.h>
#define XER_AI_HAS_FP16_HEADER 1
#else
#define XER_AI_HAS_FP16_HEADER 0
#endif

namespace XER::ML::Utils {

std::vector<float> MlTypeConverter::IntToFloat(const std::vector<int32_t>& input) {
	std::vector<float> out(input.size(), 0.0f);
	std::transform(input.begin(), input.end(), out.begin(),
								 [](int32_t value) { return static_cast<float>(value); });
	return out;
}

std::vector<int32_t> MlTypeConverter::FloatToInt(const std::vector<float>& input) {
	std::vector<int32_t> out(input.size(), 0);
	std::transform(input.begin(), input.end(), out.begin(),
								 [](float value) { return static_cast<int32_t>(value); });
	return out;
}

std::vector<uint8_t> MlTypeConverter::FloatToBytes(const std::vector<float>& input) {
	std::vector<uint8_t> out(input.size() * sizeof(float), 0U);
	if (!out.empty()) {
		std::memcpy(out.data(), input.data(), out.size());
	}
	return out;
}

std::vector<float> MlTypeConverter::BytesToFloat(const std::vector<uint8_t>& input) {
	if (input.size() < sizeof(float)) {
		return {};
	}
	const size_t count = input.size() / sizeof(float);
	std::vector<float> out(count, 0.0f);
	std::memcpy(out.data(), input.data(), count * sizeof(float));
	return out;
}

std::vector<float> MlTypeConverter::Fp16RoundTrip(const std::vector<float>& input) {
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

}  // namespace XER::ML::Utils

