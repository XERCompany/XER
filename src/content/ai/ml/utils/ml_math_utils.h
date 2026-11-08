#pragma once

#include <cstddef>
#include <vector>

namespace XER::ML::Utils {

class MlMathUtils {
 public:
	static float Dot(const std::vector<float>& a, const std::vector<float>& b);
	static float Mean(const std::vector<float>& values);
	static float Variance(const std::vector<float>& values);
	static std::vector<float> Normalize(const std::vector<float>& values);
	static std::vector<float> Softmax(const std::vector<float>& logits);
	static std::vector<float> L2Normalize(const std::vector<float>& values, float epsilon = 1e-8f);
};

}  // namespace XER::ML::Utils

