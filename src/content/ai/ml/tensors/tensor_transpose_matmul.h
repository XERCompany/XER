#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
/// Compute A^T * B efficiently (transpose-matmul fused)
Tensor TensorTransposeMatMul(const Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
