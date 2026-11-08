#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {

/// Element-wise addition of two tensors
Tensor TensorAdd(const Tensor& a, const Tensor& b);
/// In-place: a += b
void TensorAddInPlace(Tensor& a, const Tensor& b);

}  // namespace XER::ML::Tensors
