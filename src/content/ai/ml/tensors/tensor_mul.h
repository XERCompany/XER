#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
Tensor TensorMul(const Tensor& a, const Tensor& b);   ///< Hadamard product
Tensor TensorScale(const Tensor& a, float scalar);
void   TensorMulInPlace(Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
