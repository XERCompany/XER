#pragma once
#include "tensor.h"
#include <vector>

namespace XER::ML::Tensors {
/// Permute axes of a tensor (only supports 2D: axis swap / transpose)
Tensor TensorPermute(const Tensor& t, const std::vector<int>& axes);
}  // namespace XER::ML::Tensors
