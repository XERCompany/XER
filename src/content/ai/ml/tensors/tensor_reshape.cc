#include "tensor_reshape.h"

namespace XER::ML::Tensors {
Tensor TensorReshape(const Tensor& t, const std::vector<size_t>& new_shape) {
    return t.Reshape(new_shape);
}
}  // namespace XER::ML::Tensors
