#include "batch_builder.h"

#include "../data_types/tensor.h"

#include <algorithm>
#include <cstring>

namespace XER::ML::DataStructures {

std::vector<std::shared_ptr<MLData::Types::Batch>> BatchBuilder::BuildBatches(
		const DatasetBase& dataset,
		size_t batch_size,
		const std::vector<uint32_t>& sample_shape) {
	std::vector<std::shared_ptr<MLData::Types::Batch>> batches;
	if (dataset.Empty() || batch_size == 0U) {
		return batches;
	}

	for (size_t start = 0; start < dataset.Size(); start += batch_size) {
		auto batch = std::make_shared<MLData::Types::Batch>(batch_size);
		const size_t end = std::min(dataset.Size(), start + batch_size);
		for (size_t i = start; i < end; ++i) {
			const DataSample* sample = dataset.GetSample(i);
			if (sample == nullptr) {
				continue;
			}

			auto tensor = std::make_shared<MLData::Types::Tensor>(sample_shape);
			if (tensor->GetData() != nullptr && !sample->features.empty()) {
				const size_t bytes = std::min(tensor->GetMemorySize(), sample->features.size() * sizeof(float));
				std::memcpy(tensor->GetData(), sample->features.data(), bytes);
			}
			batch->AddSample(tensor);
		}
		batches.push_back(std::move(batch));
	}
	return batches;
}

}  // namespace XER::ML::DataStructures

