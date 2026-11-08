#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
Tensor TensorSub(const Tensor& a, const Tensor& b);
void TensorSubInPlace(Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
