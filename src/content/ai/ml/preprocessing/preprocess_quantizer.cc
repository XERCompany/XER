#include "preprocess_quantizer.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace XER::ML::Preprocessing {

void PreprocessQuantizer::Fit(const std::vector<std::vector<float>>& X) {
    if (X.empty()) return;
    const size_t nf = X[0].size();
    data_min_.assign(nf, std::numeric_limits<float>::max());
    data_max_.assign(nf, std::numeric_limits<float>::lowest());
    for (const auto& row : X)
        for (size_t j = 0; j < nf; ++j) {
            data_min_[j] = std::min(data_min_[j], row[j]);
            data_max_[j] = std::max(data_max_[j], row[j]);
        }
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessQuantizer::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    for (auto& row : out)
        for (size_t j = 0; j < row.size() && j < data_min_.size(); ++j) {
            float span = data_max_[j] - data_min_[j];
            float bin = (span > 1e-9f)
                ? std::floor((row[j] - data_min_[j]) / span * n_bins_)
                : 0.0f;
            row[j] = std::clamp(bin, 0.0f, static_cast<float>(n_bins_ - 1));
        }
    return out;
}

}  // namespace XER::ML::Preprocessing
