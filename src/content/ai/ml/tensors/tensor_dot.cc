#include "tensor_dot.h"

namespace XER::ML::Tensors {
float TensorDot(const Tensor& a, const Tensor& b) {
    Tensor r(a);
    r *= b;
    return r.Sum();
}
}  // namespace XER::ML::Tensors
