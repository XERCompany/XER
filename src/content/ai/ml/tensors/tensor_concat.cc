#include "tensor_concat.h"
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor TensorConcat(const std::vector<Tensor>& tensors, int axis) {
    // Only 2D axis=0 (row concat) supported as base case
    if (tensors.empty()) throw std::invalid_argument("TensorConcat: empty list");
    if (tensors.size() == 1) return tensors[0].Clone();

    const auto& s0 = tensors[0].Shape();
    if (axis != 0) throw std::invalid_argument("TensorConcat: only axis=0 supported");
    if (s0.size() != 2) throw std::invalid_argument("TensorConcat: only 2D tensors supported");

    size_t total_rows = 0;
    const size_t cols = s0[1];
    for (const auto& t : tensors) {
        if (t.Shape().size() != 2 || t.Shape()[1] != cols)
            throw std::invalid_argument("TensorConcat: shape mismatch");
        total_rows += t.Shape()[0];
    }

    Tensor out(std::vector<size_t>{total_rows, cols});
    auto& od = out.MutableData();
    Eigen::Index row = 0;
    for (const auto& t : tensors) {
        const auto& td = t.Data();
        od.middleRows(row, static_cast<Eigen::Index>(t.Shape()[0])) = td;
        row += static_cast<Eigen::Index>(t.Shape()[0]);
    }
    return out;
}

}  // namespace XER::ML::Tensors
