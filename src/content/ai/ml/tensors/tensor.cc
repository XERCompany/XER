#include "tensor.h"

#include <Eigen/Core>
#include <cmath>
#include <algorithm>
#include <random>
#include <stdexcept>

namespace XER::ML::Tensors {

Tensor::Tensor(const std::vector<size_t>& shape, Device device)
    : shape_(shape), device_(device) {
    
    if (shape_.empty()) {
        throw std::invalid_argument("Tensor shape cannot be empty");
    }
    
    size_t numel = NumElements();
    data_ = Eigen::MatrixXf::Zero(numel, 1);
}

Tensor::Tensor(const Eigen::MatrixXf& matrix, Device device)
    : data_(matrix), device_(device) {
    
    shape_ = {static_cast<size_t>(matrix.rows()),
              static_cast<size_t>(matrix.cols())};
}

Tensor::Tensor(const Tensor& other)
    : data_(other.data_), shape_(other.shape_), device_(other.device_) {}

Tensor::Tensor(Tensor&& other) noexcept
    : data_(std::move(other.data_)),
      shape_(std::move(other.shape_)),
      device_(other.device_) {}

Tensor::~Tensor() = default;

Tensor& Tensor::operator=(const Tensor& other) {
    if (this != &other) {
        data_ = other.data_;
        shape_ = other.shape_;
        device_ = other.device_;
    }
    return *this;
}

size_t Tensor::NumElements() const {
    size_t numel = 1;
    for (size_t dim : shape_) {
        numel *= dim;
    }
    return numel;
}

Eigen::MatrixXf& Tensor::MutableData() {
    if (device_ != Device::CPU) {
        throw std::runtime_error("Cannot mutate GPU tensor directly");
    }
    return data_;
}

const Eigen::MatrixXf& Tensor::Data() const {
    return data_;
}

Tensor Tensor::To(Device device) const {
    if (device_ == device) {
        return Clone();
    }

    // CPU → GPU or GPU → CPU:
    // Real CUDA transfer would use cudaMalloc + cudaMemcpyAsync.
    // Without a CUDA runtime link the data stays in the Eigen matrix (CPU host
    // memory) but we update the device tag so callers can track placement.
    // Mark the copied tensor with the requested device; all compute ops
    // will continue to use Eigen on CPU — CUDA kernels can be wired in here
    // once libtorch / a CUDA context is available.
    Tensor result(*this);
    result.device_ = device;
    return result;
}

Tensor Tensor::Reshape(const std::vector<size_t>& new_shape) const {
    if (NumElements() != Tensor(new_shape).NumElements()) {
        throw std::invalid_argument("Reshape: total elements must match");
    }
    
    Tensor result(new_shape, device_);
    result.data_ = data_;
    return result;
}

size_t Tensor::FlatIndex(const std::vector<size_t>& indices) const {
    if (indices.size() != shape_.size()) {
        throw std::out_of_range("Index dimension mismatch");
    }
    
    size_t idx = 0;
    size_t stride = 1;
    
    for (int i = shape_.size() - 1; i >= 0; --i) {
        if (indices[i] >= shape_[i]) {
            throw std::out_of_range("Index out of bounds");
        }
        idx += indices[i] * stride;
        stride *= shape_[i];
    }
    
    return idx;
}

float Tensor::GetValue(const std::vector<size_t>& indices) const {
    size_t idx = FlatIndex(indices);
    return data_(idx, 0);
}

void Tensor::SetValue(const std::vector<size_t>& indices, float value) {
    size_t idx = FlatIndex(indices);
    data_(idx, 0) = value;
}

Tensor Tensor::Clone() const {
    Tensor copy(*this);
    return copy;
}

void Tensor::Fill(float value) {
    data_.fill(value);
}

void Tensor::Uniform(float min, float max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
    
    for (int i = 0; i < data_.size(); ++i) {
        data_(i, 0) = dis(gen);
    }
}

void Tensor::Normal(float mean, float std) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<> dis(mean, std);
    
    for (int i = 0; i < data_.size(); ++i) {
        data_(i, 0) = dis(gen);
    }
}

Tensor& Tensor::operator+=(const Tensor& other) {
    if (NumElements() != other.NumElements()) {
        throw std::invalid_argument("Tensor dimension mismatch");
    }
    
    data_ += other.data_;
    return *this;
}

Tensor& Tensor::operator-=(const Tensor& other) {
    if (NumElements() != other.NumElements()) {
        throw std::invalid_argument("Tensor dimension mismatch");
    }
    
    data_ -= other.data_;
    return *this;
}

Tensor& Tensor::operator*=(const Tensor& other) {
    if (NumElements() != other.NumElements()) {
        throw std::invalid_argument("Tensor dimension mismatch");
    }
    
    data_ = data_.cwiseProduct(other.data_);
    return *this;
}

Tensor& Tensor::operator/=(const Tensor& other) {
    if (NumElements() != other.NumElements()) {
        throw std::invalid_argument("Tensor dimension mismatch");
    }
    
    data_ = data_.cwiseQuotient(other.data_);
    return *this;
}

Tensor Tensor::operator+(float scalar) const {
    Tensor result = Clone();
    result.data_.array() += scalar;
    return result;
}

Tensor Tensor::operator*(float scalar) const {
    Tensor result = Clone();
    result.data_ *= scalar;
    return result;
}

Tensor Tensor::Abs() const {
    Tensor result = Clone();
    result.data_ = result.data_.cwiseAbs();
    return result;
}

Tensor Tensor::ReLU() const {
    Tensor result = Clone();
    result.data_ = result.data_.cwiseMax(0.0f);
    return result;
}

Tensor Tensor::Sigmoid() const {
    Tensor result = Clone();
    result.data_ = 1.0f / (1.0f + (-result.data_.array()).exp());
    return result;
}

Tensor Tensor::Tanh() const {
    Tensor result = Clone();
    result.data_ = result.data_.array().tanh().matrix();
    return result;
}

Tensor Tensor::Exp() const {
    Tensor result = Clone();
    result.data_ = result.data_.array().exp().matrix();
    return result;
}

Tensor Tensor::Log() const {
    Tensor result = Clone();
    result.data_ = result.data_.array().log().matrix();
    return result;
}

Tensor Tensor::Sqrt() const {
    Tensor result = Clone();
    result.data_ = result.data_.array().sqrt().matrix();
    return result;
}

float Tensor::Sum() const {
    return data_.sum();
}

float Tensor::Mean() const {
    return data_.mean();
}

float Tensor::StdDev() const {
    float mean = Mean();
    Eigen::MatrixXf centered = data_.array() - mean;
    return std::sqrt((centered.cwiseProduct(centered).sum()) / (data_.size() - 1));
}

float Tensor::Max() const {
    return data_.maxCoeff();
}

float Tensor::Min() const {
    return data_.minCoeff();
}

Tensor Tensor::MatMul(const Tensor& other) const {
    if (shape_.size() != 2 || other.shape_.size() != 2) {
        throw std::invalid_argument("MatMul requires 2D tensors");
    }
    
    if (shape_[1] != other.shape_[0]) {
        throw std::invalid_argument("MatMul dimension mismatch");
    }
    
    Eigen::MatrixXf result = data_ * other.data_;
    
    return Tensor(result, device_);
}

Tensor Tensor::Transpose() const {
    if (shape_.size() != 2) {
        throw std::invalid_argument("Transpose requires 2D tensor");
    }
    
    Eigen::MatrixXf result = data_.transpose();
    return Tensor(result, device_);
}

float Tensor::Norm() const {
    return data_.norm();
}

} // namespace XER::ML::Tensors
