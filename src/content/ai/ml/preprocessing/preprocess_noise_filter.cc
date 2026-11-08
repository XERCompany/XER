#include "preprocess_noise_filter.h"

namespace XER::ML::Preprocessing {

std::vector<std::vector<float>> PreprocessNoiseFilter::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    const size_t half = window_ / 2;
    for (auto& row : out) {
        const size_t n = row.size();
        std::vector<float> tmp(n);
        for (size_t i = 0; i < n; ++i) {
            float sum = 0.0f; size_t count = 0;
            for (size_t k = (i >= half ? i - half : 0);
                 k <= i + half && k < n; ++k) {
                sum += row[k]; ++count;
            }
            tmp[i] = count > 0 ? sum / count : row[i];
        }
        row = std::move(tmp);
    }
    return out;
}

}  // namespace XER::ML::Preprocessing
