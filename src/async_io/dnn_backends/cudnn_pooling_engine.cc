#include "cudnn_pooling_engine.h"
 
#include <cuda_runtime.h> 
#include <cudnn.h> 

namespace XER::AsyncIO::IO::DNNBackends {

namespace {
 
cudnnPoolingMode_t ToMode(PoolingKind kind) {
    return kind == PoolingKind::Max
        ? CUDNN_POOLING_MAX
        : CUDNN_POOLING_AVERAGE_COUNT_INCLUDE_PADDING;
} 

} // namespace

CuDnnPoolingEngine::CuDnnPoolingEngine()
    : kind_(PoolingKind::Max), window_(2), stride_(2), padding_(0), configured_(false) {
    bridge_.Initialize();
}

bool CuDnnPoolingEngine::Configure(PoolingKind kind, int window, int stride, int padding) {
    if (window <= 0 || stride <= 0 || padding < 0) {
        configured_ = false;
        return false;
    }

    kind_ = kind;
    window_ = window;
    stride_ = stride;
    padding_ = padding;
    configured_ = true;
    return true;
}

bool CuDnnPoolingEngine::Forward(
    const float *deviceInput,
    float *deviceOutput,
    int batch,
    int channels,
    int height,
    int width,
    void *stream) {
    if (!configured_ || deviceInput == nullptr || deviceOutput == nullptr) {
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
    cudnnPoolingDescriptor_t poolDesc;
    if (cudnnCreateTensorDescriptor(&xDesc) != CUDNN_STATUS_SUCCESS) return false;
    if (cudnnCreateTensorDescriptor(&yDesc) != CUDNN_STATUS_SUCCESS) {
        cudnnDestroyTensorDescriptor(xDesc);
        return false;
    }
    if (cudnnCreatePoolingDescriptor(&poolDesc) != CUDNN_STATUS_SUCCESS) {
        cudnnDestroyTensorDescriptor(yDesc);
        cudnnDestroyTensorDescriptor(xDesc);
        return false;
    }

    bool ok = true;
    ok = ok && (cudnnSetTensor4dDescriptor(xDesc, CUDNN_TENSOR_NCHW, CUDNN_DATA_FLOAT, batch, channels, height, width) == CUDNN_STATUS_SUCCESS);
    ok = ok && (cudnnSetPooling2dDescriptor(poolDesc, ToMode(kind_), CUDNN_PROPAGATE_NAN, window_, window_, padding_, padding_, stride_, stride_) == CUDNN_STATUS_SUCCESS);

    int outN = 0, outC = 0, outH = 0, outW = 0;
    if (ok) {
        ok = (cudnnGetPooling2dForwardOutputDim(poolDesc, xDesc, &outN, &outC, &outH, &outW) == CUDNN_STATUS_SUCCESS);
    }
    if (ok) {
        ok = (cudnnSetTensor4dDescriptor(yDesc, CUDNN_TENSOR_NCHW, CUDNN_DATA_FLOAT, outN, outC, outH, outW) == CUDNN_STATUS_SUCCESS);
    }

    if (ok) {
        const float alpha = 1.0f;
        const float beta = 0.0f;
        ok = (cudnnPoolingForward(handle, poolDesc, &alpha, xDesc, deviceInput, &beta, yDesc, deviceOutput) == CUDNN_STATUS_SUCCESS);
    }

    cudnnDestroyPoolingDescriptor(poolDesc);
    cudnnDestroyTensorDescriptor(yDesc);
    cudnnDestroyTensorDescriptor(xDesc);
    return ok; 
}

} // namespace XER::AsyncIO::IO::DNNBackends
