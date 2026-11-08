#pragma once

#include <cuda_runtime.h>
#include <cstddef>
#include <cstdint>

namespace XER::ML::CudaOps {

/// @brief Element-wise CUDA operations on tensors
/// 
/// Provides efficient GPU kernels for element-wise mathematical operations
/// including Add, Subtract, Multiply, Divide, and custom operations.
/// Optimized for maximum memory bandwidth utilization.
class CudaElementwise {
public:
    /// Element-wise binary operation types
    enum class BinaryOp : uint8_t {
        Add = 0,
        Subtract = 1,
        Multiply = 2,
        Divide = 3,
        Power = 4,
        Maximum = 5,
        Minimum = 6
    };
    
    /// Element-wise unary operation types
    enum class UnaryOp : uint8_t {
        ReLU = 0,
        Sigmoid = 1,
        Tanh = 2,
        Abs = 3,
        Exp = 4,
        Log = 5,
        Sqrt = 6,
        Square = 7
    };
    
    /// Perform element-wise binary operation: C = op(A, B)
    /// @param op Binary operation type
    /// @param device_A Device pointer to first operand (fp32)
    /// @param device_B Device pointer to second operand (fp32)
    /// @param device_C Device pointer to output (fp32)
    /// @param elements Number of elements
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ElementwiseBinary(CudaElementwise::BinaryOp op,
                                 const float* device_A,
                                 const float* device_B,
                                 float* device_C,
                                 size_t elements,
                                 cudaStream_t stream = 0);
    
    /// Perform element-wise unary operation: B = op(A)
    /// @param op Unary operation type
    /// @param device_A Device pointer to input (fp32)
    /// @param device_B Device pointer to output (fp32)
    /// @param elements Number of elements
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ElementwiseUnary(CudaElementwise::UnaryOp op,
                                const float* device_A,
                                float* device_B,
                                size_t elements,
                                cudaStream_t stream = 0);
    
    /// Fused operation: D = scale * op(A, B)
    /// @param op Binary operation type
    /// @param scale Scaling factor
    /// @param device_A Device pointer to first operand
    /// @param device_B Device pointer to second operand
    /// @param device_D Device pointer to output
    /// @param elements Number of elements
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ElementwiseBinaryScaled(CudaElementwise::BinaryOp op,
                                          float scale,
                                          const float* device_A,
                                          const float* device_B,
                                          float* device_D,
                                          size_t elements,
                                          cudaStream_t stream = 0);
};

} // namespace XER::ML::CudaOps
