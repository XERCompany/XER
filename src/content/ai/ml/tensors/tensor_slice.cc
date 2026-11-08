#include "tensor_slice.h"
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor TensorSlice(const Tensor& t, size_t start, size_t length) {
    if (t.NumDimensions() != 2)
        throw std::invalid_argument("TensorSlice: only 2D tensors supported");
    const size_t rows = t.Shape()[0], cols = t.Shape()[1];
    if (start + length > rows)
        throw std::out_of_range("TensorSlice: out of bounds");
    Tensor out(std::vector<size_t>{length, cols});
    out.MutableData() = t.Data().middleRows(
        static_cast<Eigen::Index>(start), static_cast<Eigen::Index>(length));
    return out;
}

}  // namespace XER::ML::Tensors
