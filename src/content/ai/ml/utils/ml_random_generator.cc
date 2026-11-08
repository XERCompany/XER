#include "ml_random_generator.h"

namespace XER::ML::Utils {

MlRandomGenerator::MlRandomGenerator(uint64_t seed)
		: seed_(seed), engine_(seed) {
}

void MlRandomGenerator::SetSeed(uint64_t seed) {
	seed_ = seed;
	engine_.seed(seed);
}

uint64_t MlRandomGenerator::GetSeed() const {
	return seed_;
}

float MlRandomGenerator::UniformFloat(float min_value, float max_value) {
	std::uniform_real_distribution<float> dist(min_value, max_value);
	return dist(engine_);
}

int MlRandomGenerator::UniformInt(int min_value, int max_value) {
	std::uniform_int_distribution<int> dist(min_value, max_value);
	return dist(engine_);
}

bool MlRandomGenerator::Bernoulli(float probability) {
	std::bernoulli_distribution dist(probability);
	return dist(engine_);
}

}  // namespace XER::ML::Utils

