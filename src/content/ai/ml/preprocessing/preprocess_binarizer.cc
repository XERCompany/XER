#include "preprocess_binarizer.h"

namespace XER::ML::Preprocessing {

std::vector<std::vector<float>> PreprocessBinarizer::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    for (auto& row : out)
        for (auto& v : row) v = (v >= threshold_) ? 1.0f : 0.0f;
    return out;
}

}  // namespace XER::ML::Preprocessing
