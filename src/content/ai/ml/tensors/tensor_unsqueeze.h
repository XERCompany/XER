#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
/// Insert a dimension of size 1 at the given axis
Tensor TensorUnsqueeze(const Tensor& t, int axis);
}  // namespace XER::ML::Tensors
