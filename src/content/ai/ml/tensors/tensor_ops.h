#pragma once

#include "tensor.h"
#include <vector>

namespace XER::ML::Tensors {

/// @brief Tensor broadcasting utilities
/// 
/// Handles shape broadcasting similar to NumPy:
/// - (3,) + (1,) → (3,)
/// - (4,3,2) + (3,2) → (4,3,2)
/// - Skips dimensions of size 1
class TensorBroadcast {
public:
    /// Check if two shapes are broadcastable
    /// @param shape1 First tensor shape
    /// @param shape2 Second tensor shape
    /// @return true if broadcastable
    static bool IsCompatible(const std::vector<size_t>& shape1,
                             const std::vector<size_t>& shape2);
    
    /// Compute broadcasted output shape
    /// @param shape1 First tensor shape
    /// @param shape2 Second tensor shape
    /// @return Broadcast shape
    static std::vector<size_t> GetOutputShape(const std::vector<size_t>& shape1,
                                               const std::vector<size_t>& shape2);
    
    /// Broadcast tensor to target shape
    /// @param tensor Source tensor
    /// @param target_shape Target shape
    /// @return Broadcasted tensor
    static Tensor Broadcast(const Tensor& tensor,
                            const std::vector<size_t>& target_shape);
};

/// @brief Tensor slicing and indexing
/// 
/// Provides row/column slicing, sub-tensor extraction
class TensorSlice {
public:
    /// Get single row (2D tensor only)
    /// @param tensor Source tensor
    /// @param row_idx Row index
    /// @return Row as 1D tensor
    static Tensor GetRow(const Tensor& tensor, size_t row_idx);
    
    /// Get single column (2D tensor only)
    /// @param tensor Source tensor
    /// @param col_idx Column index
    /// @return Column as 1D tensor
    static Tensor GetColumn(const Tensor& tensor, size_t col_idx);
    
    /// Get sub-tensor (2D)
    /// @param tensor Source tensor
    /// @param row_start Starting row
    /// @param row_count Number of rows
    /// @param col_start Starting column
    /// @param col_count Number of columns
    /// @return Sub-tensor
    static Tensor GetBlock(const Tensor& tensor,
                          size_t row_start, size_t row_count,
                          size_t col_start, size_t col_count);
};

/// @brief Tensor concatenation and stacking
/// 
/// Join multiple tensors along axis
class TensorConcat {
public:
    /// Concatenate tensors along axis
    /// @param tensors Vector of tensors to concatenate
    /// @param axis Axis to concatenate along
    /// @return Concatenated tensor
    static Tensor Concatenate(const std::vector<Tensor>& tensors, int axis = 0);
    
    /// Stack tensors (creates new dimension)
    /// @param tensors Vector of tensors to stack
    /// @param axis Axis to stack along
    /// @return Stacked tensor
    static Tensor Stack(const std::vector<Tensor>& tensors, int axis = 0);
};

} // namespace XER::ML::Tensors
