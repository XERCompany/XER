#include "scaler.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <vector>

namespace XER::MLData::Preprocessing {

Scaler::Scaler(ScalingMode mode) : mode_(mode) {
}

std::shared_ptr<Types::Tensor> Scaler::Process(const Types::Tensor& input) {
    auto result = std::make_shared<Types::Tensor>(input.GetShape(), input.GetDataType());

    if (input.GetDataType() != Types::DataType::FLOAT32 || !input.GetData()) {
        if (input.GetData())
            std::memcpy(result->GetData(), input.GetData(), input.GetMemorySize());
        return result;
    }

    const float*  src = static_cast<const float*>(input.GetData());
    float*        dst = static_cast<float*>(result->GetData());
    const uint64_t n  = input.GetElementCount();

    if (n == 0) return result;

    switch (mode_) {
        case ScalingMode::MinMax: {
            float vmin = *std::min_element(src, src + n);
            float vmax = *std::max_element(src, src + n);
            float span = vmax - vmin;
            for (uint64_t i = 0; i < n; ++i)
                dst[i] = (span > 1e-9f) ? (src[i] - vmin) / span : 0.0f;
            break;
        }
        case ScalingMode::StandardScore: {
            double sum = 0.0;
            for (uint64_t i = 0; i < n; ++i) sum += src[i];
            const float mean = static_cast<float>(sum / static_cast<double>(n));
            double var = 0.0;
            for (uint64_t i = 0; i < n; ++i) {
                float d = src[i] - mean;
                var += d * d;
            }
            const float stddev = static_cast<float>(std::sqrt(var / static_cast<double>(n)));
            const float inv_std = (stddev > 1e-9f) ? (1.0f / stddev) : 1.0f;
            for (uint64_t i = 0; i < n; ++i)
                dst[i] = (src[i] - mean) * inv_std;
            break;
        }
        case ScalingMode::RobustScale: {
            // Median centering + IQR scaling.
            std::vector<float> sorted(src, src + n);
            std::sort(sorted.begin(), sorted.end());
            const float median = sorted[n / 2];
            const float q1 = sorted[n / 4];
            const float q3 = sorted[n * 3 / 4];
            const float iqr = q3 - q1;
            const float inv_iqr = (iqr > 1e-9f) ? (1.0f / iqr) : 1.0f;
            for (uint64_t i = 0; i < n; ++i)
                dst[i] = (src[i] - median) * inv_iqr;
            break;
        }
    }

    return result;
}

} // namespace XER::MLData::Preprocessing
