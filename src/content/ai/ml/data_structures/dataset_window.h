#pragma once

#include "dataset_base.h"

#include <cstddef>
#include <vector>

namespace XER::ML::DataStructures {

class DatasetWindow {
 public:
  static std::vector<DatasetBase> BuildWindows(const DatasetBase& dataset,
											   size_t window_size,
											   size_t stride);
};

}  // namespace XER::ML::DataStructures

