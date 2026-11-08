#include "dataset_shuffle.h"

#include "../utils/ml_random_generator.h"

namespace XER::ML::DataStructures {

DatasetBase DatasetShuffle::ShuffledCopy(const DatasetBase& dataset, uint64_t seed) {
	DatasetBase copy(dataset.Name() + "_shuffled");
	for (const auto& sample : dataset.Samples()) {
		copy.AddSample(sample);
	}
	ShuffleInPlace(copy, seed);
	return copy;
}

void DatasetShuffle::ShuffleInPlace(DatasetBase& dataset, uint64_t seed) {
	Utils::MlRandomGenerator rng(seed);
	auto samples = dataset.Samples();
	rng.Shuffle(samples);

	DatasetBase shuffled(dataset.Name());
	for (const auto& sample : samples) {
		shuffled.AddSample(sample);
	}
	dataset = std::move(shuffled);
}

}  // namespace XER::ML::DataStructures

