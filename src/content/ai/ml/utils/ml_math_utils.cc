#include "ml_math_utils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

#if __has_include(<Eigen/Dense>)
#include <Eigen/Dense>
#define XER_AI_HAS_EIGEN_HEADER 1
#else
#define XER_AI_HAS_EIGEN_HEADER 0
#endif

namespace XER::ML::Utils {

float MlMathUtils::Dot(const std::vector<float>& a, const std::vector<float>& b) {
	const size_t size = std::min(a.size(), b.size());
	if (size == 0U) {
		return 0.0f;
	}

#if XER_AI_HAS_EIGEN_HEADER
	Eigen::Map<const Eigen::VectorXf> a_map(a.data(), static_cast<Eigen::Index>(size));
	Eigen::Map<const Eigen::VectorXf> b_map(b.data(), static_cast<Eigen::Index>(size));
	return a_map.dot(b_map);
#else
	return std::inner_product(a.begin(), a.begin() + static_cast<std::ptrdiff_t>(size), b.begin(), 0.0f);
#endif
}

float MlMathUtils::Mean(const std::vector<float>& values) {
	if (values.empty()) {
		return 0.0f;
	}

#if XER_AI_HAS_EIGEN_HEADER
	Eigen::Map<const Eigen::VectorXf> map(values.data(), static_cast<Eigen::Index>(values.size()));
	return map.mean();
#else
	return std::accumulate(values.begin(), values.end(), 0.0f) /
				 static_cast<float>(values.size());
#endif
}

float MlMathUtils::Variance(const std::vector<float>& values) {
	if (values.size() < 2U) {
		return 0.0f;
	}

	const float mean = Mean(values);
	float sum = 0.0f;
	for (const float value : values) {
		const float delta = value - mean;
		sum += delta * delta;
	}
	return sum / static_cast<float>(values.size() - 1U);
}

std::vector<float> MlMathUtils::Normalize(const std::vector<float>& values) {
	if (values.empty()) {
		return {};
	}

	const auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
	const float min_value = *min_it;
	const float max_value = *max_it;
	const float range = max_value - min_value;
	if (range <= 0.0f) {
		return std::vector<float>(values.size(), 0.0f);
	}

	std::vector<float> out(values.size(), 0.0f);
	for (size_t i = 0; i < values.size(); ++i) {
		out[i] = (values[i] - min_value) / range;
	}
	return out;
}

std::vector<float> MlMathUtils::Softmax(const std::vector<float>& logits) {
	if (logits.empty()) {
		return {};
	}

	const float max_logit = *std::max_element(logits.begin(), logits.end());
	std::vector<float> exps(logits.size(), 0.0f);
	for (size_t i = 0; i < logits.size(); ++i) {
		exps[i] = std::exp(logits[i] - max_logit);
	}

	const float denom = std::accumulate(exps.begin(), exps.end(), 0.0f);
	if (denom <= 0.0f) {
		return std::vector<float>(logits.size(), 0.0f);
	}

	for (float& value : exps) {
		value /= denom;
	}
	return exps;
}

std::vector<float> MlMathUtils::L2Normalize(const std::vector<float>& values, float epsilon) {
	if (values.empty()) {
		return {};
	}

	const float norm = std::sqrt(std::max(0.0f, Dot(values, values)));
	if (norm <= epsilon) {
		return std::vector<float>(values.size(), 0.0f);
	}

	std::vector<float> out(values.size(), 0.0f);
	for (size_t i = 0; i < values.size(); ++i) {
		out[i] = values[i] / norm;
	}
	return out;
}

}  // namespace XER::ML::Utils

