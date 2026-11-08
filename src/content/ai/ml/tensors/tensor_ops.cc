#include "tensor_ops.h"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace XER::ML::Tensors {

bool TensorBroadcast::IsCompatible(const std::vector<size_t>& shape1,
                                    const std::vector<size_t>& shape2) {
    size_t max_dims = std::max(shape1.size(), shape2.size());
    
    for (int i = 1; i <= max_dims; ++i) {
        size_t dim1 = (i <= shape1.size()) ? shape1[shape1.size() - i] : 1;
        size_t dim2 = (i <= shape2.size()) ? shape2[shape2.size() - i] : 1;
        
        if (dim1 != dim2 && dim1 != 1 && dim2 != 1) {
            return false;
        }
    }
    
    return true;
}

std::vector<size_t> TensorBroadcast::GetOutputShape(const std::vector<size_t>& shape1,
                                                     const std::vector<size_t>& shape2) {
    if (!IsCompatible(shape1, shape2)) {
        throw std::invalid_argument("Shapes not broadcastable");
    }
    
    size_t max_dims = std::max(shape1.size(), shape2.size());
    std::vector<size_t> output_shape(max_dims);
    
    for (int i = 1; i <= max_dims; ++i) {
        size_t dim1 = (i <= shape1.size()) ? shape1[shape1.size() - i] : 1;
        size_t dim2 = (i <= shape2.size()) ? shape2[shape2.size() - i] : 1;
        output_shape[max_dims - i] = std::max(dim1, dim2);
    }
    
    return output_shape;
}

Tensor TensorBroadcast::Broadcast(const Tensor& tensor,
                                   const std::vector<size_t>& target_shape) {
    const auto& current_shape = tensor.Shape();

    if (current_shape == target_shape) {
        return tensor.Clone();
    }

    const size_t target_dims = target_shape.size();
    const size_t src_dims    = current_shape.size();

    // Validate broadcastability
    if (!IsCompatible(current_shape, target_shape)) {
        throw std::invalid_argument("Shapes are not broadcastable");
    }

    // Only supports broadcasting to 2D for now (covers the common ML case)
    if (target_dims != 2) {
        throw std::invalid_argument("Broadcast: only 2D target supported");
    }

    const size_t target_rows = target_shape[0];
    const size_t target_cols = target_shape[1];

    // Determine source rows/cols (with padding of 1 for missing dims)
    const size_t src_rows = (src_dims >= 2) ? current_shape[src_dims - 2] : 1;
    const size_t src_cols = (src_dims >= 1) ? current_shape[src_dims - 1] : 1;

    Tensor result(std::vector<size_t>{target_rows, target_cols}, tensor.GetDevice());
    const Eigen::MatrixXf& src = tensor.Data();

    for (size_t r = 0; r < target_rows; ++r) {
        const Eigen::Index sr = static_cast<Eigen::Index>((src_rows == 1) ? 0 : r);
        for (size_t c = 0; c < target_cols; ++c) {
            const Eigen::Index sc = static_cast<Eigen::Index>((src_cols == 1) ? 0 : c);
            result.MutableData()(static_cast<Eigen::Index>(r * target_cols + c), 0) =
                src(sr * static_cast<Eigen::Index>(src_cols) + sc, 0);
        }
    }

    return result;
}

Tensor TensorSlice::GetRow(const Tensor& tensor, size_t row_idx) {
    const auto& shape = tensor.Shape();
    if (shape.size() != 2) {
        throw std::invalid_argument("GetRow requires 2D tensor");
    }
    
    if (row_idx >= shape[0]) {
        throw std::out_of_range("Row index out of bounds");
    }
    
    // Create row vector
    auto row_data = tensor.Data().row(row_idx);
    return Tensor(row_data, tensor.GetDevice());
}

Tensor TensorSlice::GetColumn(const Tensor& tensor, size_t col_idx) {
    const auto& shape = tensor.Shape();
    if (shape.size() != 2) {
        throw std::invalid_argument("GetColumn requires 2D tensor");
    }
    
    if (col_idx >= shape[1]) {
        throw std::out_of_range("Column index out of bounds");
    }
    
    // Create column vector
    auto col_data = tensor.Data().col(col_idx);
    return Tensor(col_data, tensor.GetDevice());
}

Tensor TensorSlice::GetBlock(const Tensor& tensor,
                              size_t row_start, size_t row_count,
                              size_t col_start, size_t col_count) {
    const auto& shape = tensor.Shape();
    if (shape.size() != 2) {
        throw std::invalid_argument("GetBlock requires 2D tensor");
    }
    
    if (row_start + row_count > shape[0] || col_start + col_count > shape[1]) {
        throw std::out_of_range("Block out of bounds");
    }
    
    auto block_data = tensor.Data().block(row_start, col_start, row_count, col_count);
    return Tensor(block_data, tensor.GetDevice());
}

Tensor TensorConcat::Concatenate(const std::vector<Tensor>& tensors, int axis) {
    if (tensors.empty()) {
        throw std::invalid_argument("Cannot concatenate empty tensor list");
    }

    const auto& first_shape = tensors[0].Shape();
    const size_t rank = first_shape.size();

    for (const auto& t : tensors) {
        if (t.Shape().size() != rank) {
            throw std::invalid_argument("All tensors must have same rank");
        }
    }

    if (rank != 2) {
        throw std::invalid_argument("Concatenation currently supports 2D tensors only");
    }

    const int safe_axis = (axis < 0) ? static_cast<int>(rank) + axis : axis;
    if (safe_axis < 0 || static_cast<size_t>(safe_axis) >= rank) {
        throw std::invalid_argument("Axis out of range for tensor rank");
    }

    // ── axis=0: concatenate rows ─────────────────────────────────────────────
    if (safe_axis == 0) {
        const size_t cols = first_shape[1];
        size_t total_rows = 0;
        for (const auto& t : tensors) {
            if (t.Shape()[1] != cols) {
                throw std::invalid_argument("Mismatch in non-concatenation axis (cols)");
            }
            total_rows += t.Shape()[0];
        }

        Tensor result(std::vector<size_t>{total_rows, cols}, tensors[0].GetDevice());
        size_t row_offset = 0;
        for (const auto& t : tensors) {
            const size_t rows = t.Shape()[0];
            result.MutableData().block(
                static_cast<Eigen::Index>(row_offset), 0,
                static_cast<Eigen::Index>(rows),
                static_cast<Eigen::Index>(cols)) = t.Data();
            row_offset += rows;
        }
        return result;
    }

    // ── axis=1: concatenate columns ──────────────────────────────────────────
    {
        const size_t rows = first_shape[0];
        size_t total_cols = 0;
        for (const auto& t : tensors) {
            if (t.Shape()[0] != rows) {
                throw std::invalid_argument("Mismatch in non-concatenation axis (rows)");
            }
            total_cols += t.Shape()[1];
        }

        Tensor result(std::vector<size_t>{rows, total_cols}, tensors[0].GetDevice());
        size_t col_offset = 0;
        for (const auto& t : tensors) {
            const size_t tcols = t.Shape()[1];
            result.MutableData().block(
                0,
                static_cast<Eigen::Index>(col_offset),
                static_cast<Eigen::Index>(rows),
                static_cast<Eigen::Index>(tcols)) = t.Data();
            col_offset += tcols;
        }
        return result;
    }
}

Tensor TensorConcat::Stack(const std::vector<Tensor>& tensors, int /*axis*/) {
    if (tensors.empty()) {
        throw std::invalid_argument("Cannot stack empty tensor list");
    }

    // Validate all tensors have the same shape.
    const auto& first_shape = tensors[0].Shape();
    for (const auto& t : tensors) {
        if (t.Shape() != first_shape) {
            throw std::invalid_argument("Stack: all tensors must have the same shape");
        }
    }

    // Stack creates a new leading dimension of size |tensors|.
    // Result shape: [num_tensors, D1, D2, ...]
    // We flatten each tensor to a row in a 2D result:
    //   shape = [num_tensors, num_elements_per_tensor]
    const size_t num_tensors  = tensors.size();
    const size_t numel        = tensors[0].NumElements();

    Tensor result(std::vector<size_t>{num_tensors, numel}, tensors[0].GetDevice());
    for (size_t i = 0; i < num_tensors; ++i) {
        // Each tensor's data is stored flat as a column vector; copy as a row.
        result.MutableData().row(static_cast<Eigen::Index>(i)) =
            tensors[i].Data().col(0).transpose();
    }
    return result;
}

} // namespace XER::ML::Tensors
