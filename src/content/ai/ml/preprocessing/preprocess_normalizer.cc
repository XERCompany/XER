#include "preprocess_normalizer.h"
#include <cmath>

namespace XER::ML::Preprocessing {

std::vector<std::vector<float>> PreprocessNormalizer::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    for (auto& row : out) {
        float norm = 0.0f;
        for (float v : row) norm += v * v;
        norm = std::sqrt(norm);
        if (norm > 1e-9f) for (auto& v : row) v /= norm;
    }
    return out;
}

}  // namespace XER::ML::Preprocessing
