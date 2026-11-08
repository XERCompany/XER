#pragma once

#include <Eigen/Dense>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace XER::ML::Tensors {

/// @brief Dense tensor container with Eigen backend
/// 
/// Provides CPU/GPU-agnostic tensor abstraction:
/// - Stores data in Eigen::MatrixXf (dynamic floating-point matrix)
/// - Supports arbitrary shapes (via reshape/broadcasting)
/// - Integrates with linear algebra operations
/// - Can be transparently moved to/from GPU
class Tensor {
public:
    /// Tensor memory location
    enum class Device : uint8_t {
        CPU = 0,
        GPU = 1  // Maps to CUDA device memory
    };
    
    /// Create tensor with given shape
    /// @param shape Vector of dimensions [D1, D2, ..., DN]
    /// @param device CPU or GPU
    explicit Tensor(const std::vector<size_t>& shape,
                    Device device = Device::CPU);
    
    /// Create tensor from existing Eigen matrix
    /// @param matrix Eigen matrix (data is copied)
    /// @param device CPU or GPU
    explicit Tensor(const Eigen::MatrixXf& matrix,
                    Device device = Device::CPU);
    
    /// Copy constructor
    Tensor(const Tensor& other);
    
    /// Move constructor
    Tensor(Tensor&& other) noexcept;
    
    /// Destructor
    ~Tensor();
    
    /// Assignment operator
    Tensor& operator=(const Tensor& other);
    
    /// Get tensor shape
    const std::vector<size_t>& Shape() const { return shape_; }
    
    /// Get total number of elements
    size_t NumElements() const;
    
    /// Get number of dimensions
    size_t NumDimensions() const { return shape_.size(); }
    
    /// Get current device
    Device GetDevice() const { return device_; }
    
    /// Get mutable reference to underlying Eigen matrix
    /// (Only valid if on CPU)
    Eigen::MatrixXf& MutableData();
    
    /// Get const reference to underlying Eigen matrix
    const Eigen::MatrixXf& Data() const;
    
    /// Move tensor to different device (CPU ↔ GPU)
    /// @param device Target device
    /// @return New tensor on target device
    Tensor To(Device device) const;
    
    /// Reshape tensor (preserves data, changes interpretation)
    /// @param new_shape New shape vector
    /// @return New tensor with reshaped view
    /// @note Total elements must match
    Tensor Reshape(const std::vector<size_t>& new_shape) const;
    
    /// Get a scalar value at position
    /// @param indices Vector of indices
    /// @return Scalar value
    float GetValue(const std::vector<size_t>& indices) const;
    
    /// Set a scalar value at position
    /// @param indices Vector of indices
    /// @param value Value to set
    void SetValue(const std::vector<size_t>& indices, float value);
    
    /// Clone tensor (deep copy)
    Tensor Clone() const;
    
    /// Fill tensor with constant value
    /// @param value Fill value
    void Fill(float value);
    
    /// Fill tensor with random uniform values in [min, max]
    /// @param min Minimum value
    /// @param max Maximum value
    void Uniform(float min, float max);
    
    /// Fill tensor with random normal values (mean=0, std=1)
    void Normal(float mean = 0.0f, float std = 1.0f);
    
    /// Element-wise operations
    
    /// Element-wise addition: this = this + other
    Tensor& operator+=(const Tensor& other);
    
    /// Element-wise subtraction: this = this - other
    Tensor& operator-=(const Tensor& other);
    
    /// Element-wise multiplication (Hadamard product)
    Tensor& operator*=(const Tensor& other);
    
    /// Element-wise division
    Tensor& operator/=(const Tensor& other);
    
    /// Scalar operations
    
    /// Scalar addition: this + scalar
    Tensor operator+(float scalar) const;
    
    /// Scalar multiplication: this * scalar
    Tensor operator*(float scalar) const;
    
    /// Element-wise unary operations
    
    /// Element-wise absolute value
    Tensor Abs() const;
    
    /// Element-wise ReLU: max(x, 0)
    Tensor ReLU() const;
    
    /// Element-wise Sigmoid: 1 / (1 + exp(-x))
    Tensor Sigmoid() const;
    
    /// Element-wise Tanh
    Tensor Tanh() const;
    
    /// Element-wise exponential
    Tensor Exp() const;
    
    /// Element-wise natural logarithm
    Tensor Log() const;
    
    /// Element-wise square root
    Tensor Sqrt() const;
    
    /// Reduction operations
    
    /// Sum all elements
    float Sum() const;
    
    /// Mean of all elements
    float Mean() const;
    
    /// Standard deviation
    float StdDev() const;
    
    /// Maximum element
    float Max() const;
    
    /// Minimum element
    float Min() const;
    
    /// Linear algebra operations (2D tensors only)
    
    /// Matrix multiplication: result = this @ other
    /// @param other Other tensor (2D)
    /// @return Result tensor
    Tensor MatMul(const Tensor& other) const;
    
    /// Matrix transpose (2D only)
    Tensor Transpose() const;
    
    /// Compute L2 norm (Frobenius norm for matrices)
    float Norm() const;

private:
    Eigen::MatrixXf data_;
    std::vector<size_t> shape_;
    Device device_;
    
    // Helper to convert indices to flat index
    size_t FlatIndex(const std::vector<size_t>& indices) const;
};

} // namespace XER::ML::Tensors
