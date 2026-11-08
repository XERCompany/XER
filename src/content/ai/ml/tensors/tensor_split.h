#pragma once
#include "tensor.h"
#include <vector>

namespace XER::ML::Tensors {
/// Split tensor along axis=0 into `num_splits` equal parts
std::vector<Tensor> TensorSplit(const Tensor& t, int num_splits);
/// Split into parts with given sizes along axis=0
std::vector<Tensor> TensorSplitSizes(const Tensor& t, const std::vector<size_t>& sizes);
}  // namespace XER::ML::Tensors
