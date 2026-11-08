#include "tensor_squeeze.h"
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor TensorSqueeze(const Tensor& t, int axis) {
    auto shape = t.Shape();
    if (axis == -1) {
        for (size_t i = 0; i < shape.size(); ++i)
            if (shape[i] == 1) { axis = static_cast<int>(i); break; }
    }
    if (axis < 0 || static_cast<size_t>(axis) >= shape.size())
        throw std::invalid_argument("TensorSqueeze: invalid axis");
    if (shape[axis] != 1)
        throw std::invalid_argument("TensorSqueeze: dim is not 1");
    shape.erase(shape.begin() + axis);
    return t.Reshape(shape);
}

}  // namespace XER::ML::Tensors
