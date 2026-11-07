 
#include <cstddef>

#include "cudnn_bridge.h"

namespace XER::AsyncIO::IO::DNNBackends {

class CuDnnConvolutionEngine {
public:
    CuDnnConvolutionEngine();

    bool Configure(int inputChannels, int outputChannels, int kernelSize, int stride, int padding);
    bool IsConfigured() const;

    bool Forward(
        const float *deviceInput,
        const float *deviceKernel,
        const float *deviceBias,
        float *deviceOutput,
        int batch,
        int height,
        int width,
        void *stream = nullptr);

private:
    CuDnnBridge bridge_;
    int inChannels_;
    int outChannels_;
    int kernel_;
    int stride_;
    int padding_;
    bool configured_;
};

} // namespace XER::AsyncIO::IO::DNNBackends 