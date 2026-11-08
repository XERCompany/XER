#include "tensor_sub.h"

namespace XER::ML::Tensors {
Tensor TensorSub(const Tensor& a, const Tensor& b) { Tensor r(a); r -= b; return r; }
void TensorSubInPlace(Tensor& a, const Tensor& b) { a -= b; }
}  // namespace XER::ML::Tensors
