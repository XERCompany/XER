#include "dataset_sampler.h"

#include "dataset_shuffle.h"

#include <algorithm>

namespace XER::ML::DataStructures {

DatasetBase DatasetSampler::SampleFraction(const DatasetBase& dataset, float fraction, uint64_t seed) {
	if (fraction <= 0.0f || dataset.Empty()) {
		return DatasetBase(dataset.Name() + "_sampled");
	}
	const float clamped = std::clamp(fraction, 0.0f, 1.0f);
	const size_t count = static_cast<size_t>(clamped * static_cast<float>(dataset.Size()));
	return SampleCount(dataset, std::max<size_t>(1U, count), seed);
}

DatasetBase DatasetSampler::SampleCount(const DatasetBase& dataset, size_t count, uint64_t seed) {
	DatasetBase shuffled = DatasetShuffle::ShuffledCopy(dataset, seed);
	DatasetBase sampled(dataset.Name() + "_sampled");

	const size_t capped = std::min(count, shuffled.Size());
	for (size_t i = 0; i < capped; ++i) {
		const DataSample* sample = shuffled.GetSample(i);
		if (sample != nullptr) {
			sampled.AddSample(*sample);
		}
	}
	return sampled;
}

}  // namespace XER::ML::DataStructures

