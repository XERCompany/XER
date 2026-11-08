#pragma once

#include <cuda_runtime.h>
#include <cudnn.h>
#include <cstddef>

namespace XER::ML::CudaOps {

/// @brief CUDA tensor kernel operations with CuDNN acceleration
/// 
/// Provides high-level tensor operations backed by CUDA and CuDNN:
/// - Convolution (forward/backward)
/// - Pooling (max/avg forward/backward)
/// - Normalization (batch norm, layer norm)
/// - Activation (forward/backward pass)
class CudaTensorKernel {
public:
    /// Convolution operation types
    enum class ConvMode : unsigned char {
        Forward = 0,
        BackwardData = 1,
        BackwardFilter = 2
    };
    
    /// Initialize CuDNN handle (call once per GPU context)
    /// @return cudaError_t - CUDA error code
    static cudaError_t Initialize();
    
    /// Cleanup CuDNN resources
    static void Cleanup();
    
    /// Conv2D forward pass with bias
    /// @param input Device pointer to input (NCHW format)
    /// @param filter Device pointer to filter weights (OIHW format)
    /// @param bias Device pointer to bias (O elements)
    /// @param output Device pointer to output (NCHW format)
    /// @param batch_size Batch size (N)
    /// @param input_channels Input channels (C)
    /// @param input_h Input height
    /// @param input_w Input width
    /// @param output_channels Output channels (O)
    /// @param kernel_size Convolution kernel size (assume square)
    /// @param stride Convolution stride
    /// @param padding Convolution padding
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t Conv2DForward(const float* input,
                                      const float* filter,
                                      const float* bias,
                                      float* output,
                                      int batch_size,
                                      int input_channels,
                                      int input_h,
                                      int input_w,
                                      int output_channels,
                                      int kernel_size,
                                      int stride,
                                      int padding,
                                      cudaStream_t stream = 0);
    
    /// ReLU activation forward pass
    /// @param input Device pointer to input
    /// @param output Device pointer to output (same size as input)
    /// @param elements Number of elements
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ReLUForward(const float* input,
                                    float* output,
                                    size_t elements,
                                    cudaStream_t stream = 0);
    
    /// ReLU activation backward pass
    /// @param grad_output Gradient w.r.t. output
    /// @param input Original input for mask computation
    /// @param grad_input Output gradient w.r.t. input
    /// @param elements Number of elements
    /// @param stream CUDA stream for execution
    /// @return cudaError_t - CUDA error code
    static cudaError_t ReLUBackward(const float* grad_output,
                                     const float* input,
                                     float* grad_input,
                                     size_t elements,
                                     cudaStream_t stream = 0);
};

} // namespace XER::ML::CudaOps
