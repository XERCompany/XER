#include "tensor_allocator.h"

namespace XER::ML::Tensors {

std::shared_ptr<Tensor> TensorAllocator::Zeros(const std::vector<size_t>& shape) {
    auto t = std::make_shared<Tensor>(shape);
    t->Fill(0.0f);
    return t;
}

std::shared_ptr<Tensor> TensorAllocator::Ones(const std::vector<size_t>& shape) {
    auto t = std::make_shared<Tensor>(shape);
    t->Fill(1.0f);
    return t;
}

std::shared_ptr<Tensor> TensorAllocator::Empty(const std::vector<size_t>& shape) {
    return std::make_shared<Tensor>(shape);
}

std::shared_ptr<Tensor> TensorAllocator::Clone(const Tensor& src) {
    return std::make_shared<Tensor>(src.Clone());
}

}  // namespace XER::ML::Tensors
