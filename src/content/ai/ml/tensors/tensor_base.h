#pragma once

#include "tensor.h"
#include <cstddef>
#include <vector>

namespace XER::ML::Tensors {

/// Base utilities: shape arithmetic, strides, flat-index conversion
struct TensorBase {
    /// Compute strides from shape (row-major, last dim stride=1)
    static std::vector<size_t> ComputeStrides(const std::vector<size_t>& shape);

    /// Total element count from shape
    static size_t NumElements(const std::vector<size_t>& shape);

    /// Flat index from multi-dim indices and strides
    static size_t FlatIndex(const std::vector<size_t>& indices,
                             const std::vector<size_t>& strides);

    /// True if two shapes are broadcast-compatible
    static bool BroadcastCompatible(const std::vector<size_t>& a,
                                     const std::vector<size_t>& b);

    /// Return broadcasted output shape
    static std::vector<size_t> BroadcastShape(const std::vector<size_t>& a,
                                               const std::vector<size_t>& b);
};

}  // namespace XER::ML::Tensors
