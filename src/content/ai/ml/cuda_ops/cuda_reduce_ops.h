#pragma once

#include <cuda_runtime.h>
#include <cstddef>

namespace XER::ML::CudaOps {

/// @brief Tensor reduction operations on GPU
/// 
/// Implements efficient GPU kernels for reduction operations:
/// Sum, Mean, Max, Min across tensor dimensions.
/// Optimized for different data types and reduction strategies.
class CudaReduceOps {
public:
    /// Reduction operation types
    enum class ReduceOp : unsigned char {
        Sum = 0,      ///< Sum all elements
        Mean = 1,     ///< Average of all elements
        Max = 2,      ///< Maximum element
        Min = 3,      ///< Minimum element
        StdDev = 4    ///< Standard deviation
    };
    
    /// Reduce tensor to scalar: result = reduce_op(tensor)
    /// @param op Reduction operation type
    /// @param device_input Device pointer to input tensor (fp32)
    /// @param device_output Device pointer to output scalar (fp32)
    /// @param elements Number of elements in input
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ReduceToScalar(ReduceOp op,
                                       const float* device_input,
                                       float* device_output,
                                       size_t elements,
                                       cudaStream_t stream = 0);
    
    /// Reduce along single axis: output[i] = reduce_op(input[:, i, :])
    /// @param op Reduction operation type
    /// @param device_input Device pointer to input tensor
    /// @param device_output Device pointer to output tensor
    /// @param input_numel Total number of elements in input
    /// @param axis Axis to reduce along
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ReduceAlongAxis(ReduceOp op,
                                        const float* device_input,
                                        float* device_output,
                                        size_t input_numel,
                                        int axis,
                                        cudaStream_t stream = 0);
};

} // namespace XER::ML::CudaOps
