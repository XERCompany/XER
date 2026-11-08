#include "tensor_cross.h"
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor TensorCross(const Tensor& a, const Tensor& b) {
    if (a.NumElements() != 3 || b.NumElements() != 3)
        throw std::invalid_argument("TensorCross requires 3-element tensors");
    const auto& ad = a.Data();
    const auto& bd = b.Data();
    Tensor out(std::vector<size_t>{3});
    auto& od = out.MutableData();
    od(0, 0) = ad(1, 0) * bd(2, 0) - ad(2, 0) * bd(1, 0);
    od(1, 0) = ad(2, 0) * bd(0, 0) - ad(0, 0) * bd(2, 0);
    od(2, 0) = ad(0, 0) * bd(1, 0) - ad(1, 0) * bd(0, 0);
    return out;
}

}  // namespace XER::ML::Tensors
