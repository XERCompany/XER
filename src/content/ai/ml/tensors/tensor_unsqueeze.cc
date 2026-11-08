#include "tensor_unsqueeze.h"
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor TensorUnsqueeze(const Tensor& t, int axis) {
    auto shape = t.Shape();
    if (axis < 0) axis = static_cast<int>(shape.size()) + axis + 1;
    if (axis < 0 || static_cast<size_t>(axis) > shape.size())
        throw std::invalid_argument("TensorUnsqueeze: invalid axis");
    shape.insert(shape.begin() + axis, 1);
    return t.Reshape(shape);
}

}  // namespace XER::ML::Tensors
