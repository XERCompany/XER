#include "tensor_transpose.h"

namespace XER::ML::Tensors {
Tensor TensorTranspose(const Tensor& t) { return t.Transpose(); }
}  // namespace XER::ML::Tensors
