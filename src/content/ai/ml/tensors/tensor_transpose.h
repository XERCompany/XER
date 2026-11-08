#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
/// Transpose of a 2D tensor
Tensor TensorTranspose(const Tensor& t);
}  // namespace XER::ML::Tensors
