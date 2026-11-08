#include "tensor_base.h"
#include <algorithm>
#include <stdexcept>

namespace XER::ML::Tensors {

std::vector<size_t> TensorBase::ComputeStrides(const std::vector<size_t>& shape) {
    std::vector<size_t> strides(shape.size(), 1);
    for (int i = static_cast<int>(shape.size()) - 2; i >= 0; --i)
        strides[i] = strides[i + 1] * shape[i + 1];
    return strides;
}

size_t TensorBase::NumElements(const std::vector<size_t>& shape) {
    size_t n = 1;
    for (auto d : shape) n *= d;
    return n;
}

size_t TensorBase::FlatIndex(const std::vector<size_t>& indices,
                               const std::vector<size_t>& strides) {
    size_t idx = 0;
    for (size_t i = 0; i < indices.size(); ++i)
        idx += indices[i] * strides[i];
    return idx;
}

bool TensorBase::BroadcastCompatible(const std::vector<size_t>& a,
                                      const std::vector<size_t>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i] && a[i] != 1 && b[i] != 1) return false;
    return true;
}

std::vector<size_t> TensorBase::BroadcastShape(const std::vector<size_t>& a,
                                                 const std::vector<size_t>& b) {
    if (!BroadcastCompatible(a, b))
        throw std::invalid_argument("Shapes not broadcast-compatible");
    std::vector<size_t> out(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        out[i] = std::max(a[i], b[i]);
    return out;
}

}  // namespace XER::ML::Tensors
