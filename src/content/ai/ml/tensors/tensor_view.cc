#include "tensor_view.h"

namespace XER::ML::Tensors {
Tensor TensorView(const Tensor& t, const std::vector<size_t>& new_shape) {
    return t.Reshape(new_shape);
}
}  // namespace XER::ML::Tensors
