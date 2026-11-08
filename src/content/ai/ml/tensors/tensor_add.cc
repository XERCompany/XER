#include "tensor_add.h"

namespace XER::ML::Tensors {

Tensor TensorAdd(const Tensor& a, const Tensor& b) {
    Tensor r(a);
    r += b;
    return r;
}

void TensorAddInPlace(Tensor& a, const Tensor& b) {
    a += b;
}

}  // namespace XER::ML::Tensors
