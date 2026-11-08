#include "tensor_matmul.h"

namespace XER::ML::Tensors {
Tensor TensorMatMul(const Tensor& a, const Tensor& b) { return a.MatMul(b); }
}  // namespace XER::ML::Tensors
