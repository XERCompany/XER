#include "tensor_split.h"
#include <stdexcept>

namespace XER::ML::Tensors {

std::vector<Tensor> TensorSplit(const Tensor& t, int num_splits) {
    if (t.Shape().size() != 2) throw std::invalid_argument("TensorSplit: only 2D");
    const size_t rows = t.Shape()[0];
    const size_t cols = t.Shape()[1];
    if (rows % num_splits != 0) throw std::invalid_argument("TensorSplit: uneven split");
    const size_t chunk = rows / num_splits;
    std::vector<Tensor> out;
    for (int i = 0; i < num_splits; ++i) {
        Tensor part(std::vector<size_t>{chunk, cols});
        part.MutableData() = t.Data().middleRows(
            static_cast<Eigen::Index>(i * chunk), static_cast<Eigen::Index>(chunk));
        out.push_back(std::move(part));
    }
    return out;
}

std::vector<Tensor> TensorSplitSizes(const Tensor& t, const std::vector<size_t>& sizes) {
    if (t.Shape().size() != 2) throw std::invalid_argument("TensorSplitSizes: only 2D");
    const size_t cols = t.Shape()[1];
    std::vector<Tensor> out;
    Eigen::Index row = 0;
    for (size_t sz : sizes) {
        Tensor part(std::vector<size_t>{sz, cols});
        part.MutableData() = t.Data().middleRows(row, static_cast<Eigen::Index>(sz));
        row += static_cast<Eigen::Index>(sz);
        out.push_back(std::move(part));
    }
    return out;
}

}  // namespace XER::ML::Tensors
