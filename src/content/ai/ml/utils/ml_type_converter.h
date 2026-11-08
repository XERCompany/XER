#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace XER::ML::Utils {

class MlTypeConverter {
 public:
	static std::vector<float> IntToFloat(const std::vector<int32_t>& input);
	static std::vector<int32_t> FloatToInt(const std::vector<float>& input);
	static std::vector<uint8_t> FloatToBytes(const std::vector<float>& input);
	static std::vector<float> BytesToFloat(const std::vector<uint8_t>& input);
	static std::vector<float> Fp16RoundTrip(const std::vector<float>& input);
};

}  // namespace XER::ML::Utils

