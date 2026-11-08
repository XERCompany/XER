#pragma once

#include "tensor.h"
#include <memory>
#include <vector>

namespace XER::ML::Tensors {

/// Factory for allocating Tensors (CPU or GPU)
struct TensorAllocator {
    /// Allocate a zero-initialised CPU tensor
    static std::shared_ptr<Tensor> Zeros(const std::vector<size_t>& shape);

    /// Allocate a ones-filled CPU tensor
    static std::shared_ptr<Tensor> Ones(const std::vector<size_t>& shape);

    /// Allocate an uninitialised CPU tensor
    static std::shared_ptr<Tensor> Empty(const std::vector<size_t>& shape);

    /// Clone existing tensor (deep copy)
    static std::shared_ptr<Tensor> Clone(const Tensor& src);
};

}  // namespace XER::ML::Tensors
