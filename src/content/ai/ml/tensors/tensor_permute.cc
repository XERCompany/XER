#include "tensor_permute.h"
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor TensorPermute(const Tensor& t, const std::vector<int>& axes) {
    if (t.NumDimensions() == 2 && axes.size() == 2 &&
        axes[0] == 1 && axes[1] == 0)
        return t.Transpose();
    if (axes.size() == t.NumDimensions()) {
        bool identity = true;
        for (int i = 0; i < static_cast<int>(axes.size()); ++i)
            if (axes[i] != i) { identity = false; break; }
        if (identity) return t.Clone();
    }
    throw std::invalid_argument("TensorPermute: unsupported axis permutation");
}

}  // namespace XER::ML::Tensors
