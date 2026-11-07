#include <cstddef>

#include "cudnn_bridge.h"

namespace XER::AsyncIO::IO::DNNBackends {

enum class PoolingKind {
    Max,
    Average
};

class CuDnnPoolingEngine {
public:
    CuDnnPoolingEngine();

    bool Configure(PoolingKind kind, int window, int stride, int padding);
    bool Forward(
        const float *deviceInput,
        float *deviceOutput,
        int batch,
        int channels,
        int height,
        int width,
        void *stream = nullptr);

private:
    CuDnnBridge bridge_;
    PoolingKind kind_;
    int window_;
    int stride_;
    int padding_;
    bool configured_;
};

} // namespace XER::AsyncIO::IO::DNNBackends
 
