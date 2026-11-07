#include <cstddef>

#include "cudnn_bridge.h"

namespace XER::AsyncIO::IO::DNNBackends {

enum class ActivationKind {
    Relu,
    Tanh,
    Sigmoid,
    Elu
};

class CuDnnActivationEngine {
public:
    CuDnnActivationEngine();

    bool Configure(ActivationKind kind, double coef = 0.0);
    bool ForwardInplace(float *deviceData, std::size_t elementCount, void *stream = nullptr);

    bool IsReady() const;

private:
    CuDnnBridge bridge_;
    ActivationKind kind_;
    double coef_;
};

} // namespace XER::AsyncIO::IO::DNNBackends
 