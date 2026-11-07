#include "cudnn_convolution_engine.h"
 
#include <cuda_runtime.h> 
#include <cudnn.h> 

namespace XER::AsyncIO::IO::DNNBackends {

CuDnnConvolutionEngine::CuDnnConvolutionEngine()
    : inChannels_(0), outChannels_(0), kernel_(0), stride_(1), padding_(0), configured_(false) {
    bridge_.Initialize();
}

bool CuDnnConvolutionEngine::Configure(int inputChannels, int outputChannels, int kernelSize, int stride, int padding) {
    if (inputChannels <= 0 || outputChannels <= 0 || kernelSize <= 0 || stride <= 0 || padding < 0) {
        configured_ = false;
        return false;
    }

    inChannels_ = inputChannels;
    outChannels_ = outputChannels;
    kernel_ = kernelSize;
    stride_ = stride;
    padding_ = padding;
    configured_ = true;
    return true;
}

bool CuDnnConvolutionEngine::IsConfigured() const {
    return configured_;
}

bool CuDnnConvolutionEngine::Forward(
    const float *deviceInput,
    const float *deviceKernel,
    const float *deviceBias,
    float *deviceOutput,
    int batch,
    int height,
    int width,
    void *stream) {
    (void)deviceBias;
    if (!configured_ || deviceInput == nullptr || deviceKernel == nullptr || deviceOutput == nullptr) {
        return false;
    }
 
    if (!bridge_.Initialize()) {
        return false;
    }

    auto *handle = static_cast<cudnnHandle_t>(bridge_.Handle());
    if (stream != nullptr) {
        if (cudnnSetStream(handle, static_cast<cudaStream_t>(stream)) != CUDNN_STATUS_SUCCESS) {
            return false;
        }
    }

    cudnnTensorDescriptor_t xDesc, yDesc;
    cudnnFilterDescriptor_t wDesc;
    cudnnConvolutionDescriptor_t convDesc;
    if (cudnnCreateTensorDescriptor(&xDesc) != CUDNN_STATUS_SUCCESS) return false;
    if (cudnnCreateTensorDescriptor(&yDesc) != CUDNN_STATUS_SUCCESS) {
        cudnnDestroyTensorDescriptor(xDesc);
        return false;
    }
    if (cudnnCreateFilterDescriptor(&wDesc) != CUDNN_STATUS_SUCCESS) {
        cudnnDestroyTensorDescriptor(yDesc);
        cudnnDestroyTensorDescriptor(xDesc);
        return false;
    }
    if (cudnnCreateConvolutionDescriptor(&convDesc) != CUDNN_STATUS_SUCCESS) {
        cudnnDestroyFilterDescriptor(wDesc);
        cudnnDestroyTensorDescriptor(yDesc);
        cudnnDestroyTensorDescriptor(xDesc);
        return false;
    }

    bool ok = true;
    ok = ok && (cudnnSetTensor4dDescriptor(xDesc, CUDNN_TENSOR_NCHW, CUDNN_DATA_FLOAT, batch, inChannels_, height, width) == CUDNN_STATUS_SUCCESS);
    ok = ok && (cudnnSetFilter4dDescriptor(wDesc, CUDNN_DATA_FLOAT, CUDNN_TENSOR_NCHW, outChannels_, inChannels_, kernel_, kernel_) == CUDNN_STATUS_SUCCESS);
    ok = ok && (cudnnSetConvolution2dDescriptor(convDesc, padding_, padding_, stride_, stride_, 1, 1, CUDNN_CROSS_CORRELATION, CUDNN_DATA_FLOAT) == CUDNN_STATUS_SUCCESS);

    int outN = 0;
    int outC = 0;
    int outH = 0;
    int outW = 0;
    if (ok) {
        ok = (cudnnGetConvolution2dForwardOutputDim(convDesc, xDesc, wDesc, &outN, &outC, &outH, &outW) == CUDNN_STATUS_SUCCESS);
    }
    if (ok) {
        ok = (cudnnSetTensor4dDescriptor(yDesc, CUDNN_TENSOR_NCHW, CUDNN_DATA_FLOAT, outN, outC, outH, outW) == CUDNN_STATUS_SUCCESS);
    }

    if (ok) {
        const float alpha = 1.0f;
        const float beta = 0.0f;
        ok = (cudnnConvolutionForward(
            handle,
            &alpha,
            xDesc,
            deviceInput,
            wDesc,
            deviceKernel,
            convDesc,
            CUDNN_CONVOLUTION_FWD_ALGO_IMPLICIT_GEMM,
            nullptr,
            0,
            &beta,
            yDesc,
            deviceOutput) == CUDNN_STATUS_SUCCESS);
    }

    cudnnDestroyConvolutionDescriptor(convDesc);
    cudnnDestroyFilterDescriptor(wDesc);
    cudnnDestroyTensorDescriptor(yDesc);
    cudnnDestroyTensorDescriptor(xDesc);
    return ok; 
}

} // namespace XER::AsyncIO::IO::DNNBackends
