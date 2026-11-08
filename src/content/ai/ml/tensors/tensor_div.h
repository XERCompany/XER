#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
Tensor TensorDiv(const Tensor& a, const Tensor& b);
void   TensorDivInPlace(Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
