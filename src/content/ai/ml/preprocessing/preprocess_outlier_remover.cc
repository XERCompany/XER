#include "preprocess_outlier_remover.h"
#include <algorithm>

namespace XER::ML::Preprocessing {

void PreprocessOutlierRemover::Fit(const std::vector<std::vector<float>>& X) {
    if (X.empty()) return;
    const size_t nf = X[0].size();
    lower_.resize(nf); upper_.resize(nf);
    for (size_t j = 0; j < nf; ++j) {
        std::vector<float> col;
        col.reserve(X.size());
        for (const auto& row : X) col.push_back(row[j]);
        std::sort(col.begin(), col.end());
        const float q1 = col[col.size() / 4];
        const float q3 = col[col.size() * 3 / 4];
        const float iqr = q3 - q1;
        lower_[j] = q1 - iqr_factor_ * iqr;
        upper_[j] = q3 + iqr_factor_ * iqr;
    }
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessOutlierRemover::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    for (auto& row : out)
        for (size_t j = 0; j < row.size() && j < lower_.size(); ++j)
            if (row[j] < lower_[j] || row[j] > upper_[j]) row[j] = 0.0f;
    return out;
}

}  // namespace XER::ML::Preprocessing
