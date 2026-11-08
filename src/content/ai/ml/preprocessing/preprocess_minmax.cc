#include "preprocess_minmax.h"
#include <algorithm>
#include <limits>

namespace XER::ML::Preprocessing {

void PreprocessMinMax::Fit(const std::vector<std::vector<float>>& X) {
    if (X.empty()) return;
    const size_t nf = X[0].size();
    data_min_.assign(nf, std::numeric_limits<float>::max());
    data_max_.assign(nf, std::numeric_limits<float>::lowest());
    for (auto& row : X)
        for (size_t j = 0; j < nf; ++j) {
            data_min_[j] = std::min(data_min_[j], row[j]);
            data_max_[j] = std::max(data_max_[j], row[j]);
        }
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessMinMax::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    const float range = range_max_ - range_min_;
    for (auto& row : out)
        for (size_t j = 0; j < row.size() && j < data_min_.size(); ++j) {
            float span = data_max_[j] - data_min_[j];
            row[j] = (span > 1e-9f)
                     ? range_min_ + (row[j] - data_min_[j]) / span * range
                     : range_min_;
        }
    return out;
}

}  // namespace XER::ML::Preprocessing
