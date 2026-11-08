#include "tensor_mul.h"

namespace XER::ML::Tensors {
Tensor TensorMul(const Tensor& a, const Tensor& b) { Tensor r(a); r *= b; return r; }
Tensor TensorScale(const Tensor& a, float s) { return a * s; }
void   TensorMulInPlace(Tensor& a, const Tensor& b) { a *= b; }
}  // namespace XER::ML::Tensors
