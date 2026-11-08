#include "normalizer.h"

#include <cstring>

namespace XER::MLData::Preprocessing {

Normalizer::Normalizer() : mean_(0.0f), stddev_(1.0f) {
}

std::shared_ptr<Types::Tensor> Normalizer::Process(const Types::Tensor& input) {
    auto result = std::make_shared<Types::Tensor>(input.GetShape(), input.GetDataType());

    if (input.GetDataType() != Types::DataType::FLOAT32 || !input.GetData()) {
        // Copy raw bytes as-is for non-float types.
        std::memcpy(result->GetData(), input.GetData(), input.GetMemorySize());
        return result;
    }

    const float* src = static_cast<const float*>(input.GetData());
    float*       dst = static_cast<float*>(result->GetData());
    const uint64_t n = input.GetElementCount();
    const float  inv_std = (stddev_ != 0.0f) ? (1.0f / stddev_) : 1.0f;

    for (uint64_t i = 0; i < n; ++i) {
        dst[i] = (src[i] - mean_) * inv_std;
    }

    return result;
}

} // namespace XER::MLData::Preprocessing
