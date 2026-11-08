#include "tensor_transpose_matmul.h"

namespace XER::ML::Tensors {
Tensor TensorTransposeMatMul(const Tensor& a, const Tensor& b) {
    return a.Transpose().MatMul(b);
}
}  // namespace XER::ML::Tensors
