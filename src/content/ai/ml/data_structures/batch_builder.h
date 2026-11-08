#pragma once

#include "dataset_base.h"

#include "../data_types/batch.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace XER::ML::DataStructures {

class BatchBuilder {
 public:
  static std::vector<std::shared_ptr<MLData::Types::Batch>> BuildBatches(
	  const DatasetBase& dataset,
	  size_t batch_size,
	  const std::vector<uint32_t>& sample_shape);
};

}  // namespace XER::ML::DataStructures

