#include "cudnn_activation_engine.h"
  
#include <cudnn.h> 

namespace XER::AsyncIO::IO::DNNBackends {

namespace {
 
cudnnActivationMode_t ToCudnnActivation(ActivationKind kind) {
    switch (kind) {
    case ActivationKind::Relu: return CUDNN_ACTIVATION_RELU;
    case ActivationKind::Tanh: return CUDNN_ACTIVATION_TANH;
    case ActivationKind::Sigmoid: return CUDNN_ACTIVATION_SIGMOID;
    case ActivationKind::Elu: return CUDNN_ACTIVATION_ELU;
    }
    return CUDNN_ACTIVATION_RELU;
} 

} // namespace

CuDnnActivationEngine::CuDnnActivationEngine()
    : kind_(ActivationKind::Relu), coef_(0.0) {
    bridge_.Initialize();
}

bool CuDnnActivationEngine::Configure(ActivationKind kind, double coef) {
    kind_ = kind;
    coef_ = coef;
    return true;
}

bool CuDnnActivationEngine::ForwardInplace(float *deviceData, std::size_t elementCount, void *stream) {
    if (deviceData == nullptr || elementCount == 0) {
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

    cudnnTensorDescriptor_t tensorDesc;
    cudnnActivationDescriptor_t actDesc;
    if (cudnnCreateTensorDescriptor(&tensorDesc) != CUDNN_STATUS_SUCCESS) {
        return false;
    }
    if (cudnnCreateActivationDescriptor(&actDesc) != CUDNN_STATUS_SUCCESS) {
        cudnnDestroyTensorDescriptor(tensorDesc);
        return false;
    }

    const int n = 1;
    const int c = static_cast<int>(elementCount);
    const int h = 1;
    const int w = 1;

    bool ok = true;
    ok = ok && (cudnnSetTensor4dDescriptor(tensorDesc, CUDNN_TENSOR_NCHW, CUDNN_DATA_FLOAT, n, c, h, w) == CUDNN_STATUS_SUCCESS);
    ok = ok && (cudnnSetActivationDescriptor(actDesc, ToCudnnActivation(kind_), CUDNN_PROPAGATE_NAN, coef_) == CUDNN_STATUS_SUCCESS);

    const float alpha = 1.0f;
    const float beta = 0.0f;
    if (ok) {
        ok = (cudnnActivationForward(handle, actDesc, &alpha, tensorDesc, deviceData, &beta, tensorDesc, deviceData) == CUDNN_STATUS_SUCCESS);
    }

    cudnnDestroyActivationDescriptor(actDesc);
    cudnnDestroyTensorDescriptor(tensorDesc);
    return ok; 
}

bool CuDnnActivationEngine::IsReady() const {
    return bridge_.IsReady();
}

} // namespace XER::AsyncIO::IO::DNNBackends
