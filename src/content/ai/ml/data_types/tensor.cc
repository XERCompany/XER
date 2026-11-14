#include "tensor.h"

#include <Eigen/Core>

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace XER::MLData::Types {

static size_t GetTypeSize(DataType dtype) {
    switch (dtype) {
        case DataType::FLOAT32: return 4;
        case DataType::FLOAT64: return 8;
        case DataType::INT32: return 4;
        case DataType::INT64: return 8;
        case DataType::UINT32: return 4;
        case DataType::UINT8: return 1;
        default: return 0;
    }
}

Tensor::Tensor(const std::vector<uint32_t>& shape, DataType dtype)
    : shape_(shape), data_type_(dtype), element_count_(1), data_(nullptr), memory_size_(0) {

    for (auto s : shape_) {
        element_count_ *= s;
    }

    Allocate();
}

Tensor::~Tensor() {
    Release();
}

Tensor::Tensor(const Tensor& other)
    : shape_(other.shape_),
      data_type_(other.data_type_),
      element_count_(other.element_count_),
      data_(nullptr),
      memory_size_(other.memory_size_) {
    Allocate();
    if (data_ != nullptr && other.data_ != nullptr && memory_size_ > 0) {
        std::memcpy(data_, other.data_, memory_size_);
    }
}

Tensor& Tensor::operator=(const Tensor& other) {
    if (this == &other) {
        return *this;
    }

    Release();
    shape_ = other.shape_;
    data_type_ = other.data_type_;
    element_count_ = other.element_count_;
    memory_size_ = other.memory_size_;

    Allocate();
    if (data_ != nullptr && other.data_ != nullptr && memory_size_ > 0) {
        std::memcpy(data_, other.data_, memory_size_);
    }
    return *this;
}

Tensor::Tensor(Tensor&& other) noexcept
    : shape_(std::move(other.shape_)),
      data_type_(other.data_type_),
      element_count_(other.element_count_),
      data_(other.data_),
      memory_size_(other.memory_size_) {
    other.element_count_ = 0;
    other.data_ = nullptr;
    other.memory_size_ = 0;
}

Tensor& Tensor::operator=(Tensor&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    Release();
    shape_ = std::move(other.shape_);
    data_type_ = other.data_type_;
    element_count_ = other.element_count_;
    data_ = other.data_;
    memory_size_ = other.memory_size_;

    other.element_count_ = 0;
    other.data_ = nullptr;
    other.memory_size_ = 0;
    return *this;
}

void Tensor::Zero() {
    if (data_ != nullptr && memory_size_ > 0) {
        std::memset(data_, 0, memory_size_);
    }
}

bool Tensor::Allocate() {
    const size_t type_size = GetTypeSize(data_type_);
    if (type_size == 0U) {
        memory_size_ = 0;
        data_ = nullptr;
        return false;
    }

    if (element_count_ > 0U &&
        element_count_ > (std::numeric_limits<size_t>::max() / type_size)) {
        memory_size_ = 0;
        data_ = nullptr;
        return false;
    }

    memory_size_ = static_cast<size_t>(element_count_) * type_size;
    if (memory_size_ == 0U) {
        data_ = nullptr;
        return true;
    }

    // Eigen's aligned allocator keeps tensor payloads compatible with the
    // SIMD alignment expected by Eigen/oneDNN/MKL kernels (throws on OOM).
    try {
        data_ = Eigen::internal::aligned_malloc(memory_size_);
    } catch (const std::bad_alloc&) {
        memory_size_ = 0;
        return false;
    }
    if (data_ == nullptr) {
        memory_size_ = 0;
        return false;
    }
    std::memset(data_, 0, memory_size_);
    return true;
}

void Tensor::Release() {
    if (data_ != nullptr) {
        Eigen::internal::aligned_free(data_);
        data_ = nullptr;
    }
}

} // namespace XER::MLData::Types
