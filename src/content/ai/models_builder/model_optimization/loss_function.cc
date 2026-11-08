#include "loss_function.h"

namespace XER::ModelsBuilder::Optimization {

float MSELoss::Compute(const float* predictions, const float* targets, size_t size) {
    if (!predictions || !targets || size == 0) return 0.0f;
    
    float sum = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        float diff = predictions[i] - targets[i];
        sum += diff * diff;
    }
    return sum / size;
}

} // namespace XER::ModelsBuilder::Optimization
