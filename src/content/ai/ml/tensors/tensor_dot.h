#pragma once
#include "tensor.h"

namespace XER::ML::Tensors {
/// Dot product of two flat (1-D) tensors; for 2D tensors use TensorMatMul
float TensorDot(const Tensor& a, const Tensor& b);
}  // namespace XER::ML::Tensors
