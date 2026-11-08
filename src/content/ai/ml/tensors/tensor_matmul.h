#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
/// Matrix multiplication of two 2D tensors
Tensor TensorMatMul(const Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
