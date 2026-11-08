#pragma once

#include "dataset_base.h"

#include <cstddef>
#include <cstdint>

namespace XER::ML::DataStructures {

class DatasetSampler {
 public:
	static DatasetBase SampleFraction(const DatasetBase& dataset, float fraction, uint64_t seed);
	static DatasetBase SampleCount(const DatasetBase& dataset, size_t count, uint64_t seed);
};

}  // namespace XER::ML::DataStructures

