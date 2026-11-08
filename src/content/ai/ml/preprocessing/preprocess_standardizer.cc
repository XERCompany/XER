#include "preprocess_standardizer.h"
#include <cmath>
#include <stdexcept>

namespace XER::ML::Preprocessing {

void PreprocessStandardizer::Fit(const std::vector<std::vector<float>>& X) {
    if (X.empty()) return;
    const size_t nf = X[0].size();
    const float n = static_cast<float>(X.size());
    mean_.assign(nf, 0.0f);
    std_.assign(nf, 1.0f);
    for (auto& row : X) for (size_t j = 0; j < nf; ++j) mean_[j] += row[j];
    for (auto& m : mean_) m /= n;
    std::vector<float> var(nf, 0.0f);
    for (auto& row : X) for (size_t j = 0; j < nf; ++j) {
        float d = row[j] - mean_[j]; var[j] += d * d;
    }
    for (size_t j = 0; j < nf; ++j) std_[j] = std::sqrt(var[j] / n + 1e-9f);
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessStandardizer::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    for (auto& row : out)
        for (size_t j = 0; j < row.size() && j < mean_.size(); ++j)
            row[j] = (row[j] - mean_[j]) / std_[j];
    return out;
}

}  // namespace XER::ML::Preprocessing
