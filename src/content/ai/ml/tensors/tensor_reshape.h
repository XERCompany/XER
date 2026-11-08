#pragma once
#include "tensor.h"
#include <vector>

namespace XER::ML::Tensors {
/// Reshape a tensor to new_shape (total elements must match)
Tensor TensorReshape(const Tensor& t, const std::vector<size_t>& new_shape);
}  // namespace XER::ML::Tensors
