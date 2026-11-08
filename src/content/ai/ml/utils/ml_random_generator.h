#pragma once

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace XER::ML::Utils {

class MlRandomGenerator {
 public:
	explicit MlRandomGenerator(uint64_t seed = std::random_device{}());

	void SetSeed(uint64_t seed);
	uint64_t GetSeed() const;

	float UniformFloat(float min_value = 0.0f, float max_value = 1.0f);
	int UniformInt(int min_value, int max_value);
	bool Bernoulli(float probability);

	template <typename T>
	void Shuffle(std::vector<T>& values) {
		std::shuffle(values.begin(), values.end(), engine_);
	}

 private:
	uint64_t seed_;
	std::mt19937_64 engine_;
};

}  // namespace XER::ML::Utils

