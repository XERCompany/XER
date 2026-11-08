#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
/// Cross product of two 3-element tensors (returns 3-element tensor)
Tensor TensorCross(const Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
