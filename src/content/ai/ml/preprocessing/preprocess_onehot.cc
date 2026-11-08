#include "preprocess_onehot.h"
#include <algorithm>

namespace XER::ML::Preprocessing {

void PreprocessOnehot::Fit(const std::vector<int>& labels) {
    int mx = 0;
    for (int l : labels) if (l > mx) mx = l;
    n_classes_ = static_cast<size_t>(mx + 1);
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessOnehot::Transform(
        const std::vector<int>& labels) const {
    std::vector<std::vector<float>> out;
    out.reserve(labels.size());
    for (int l : labels) {
        std::vector<float> row(n_classes_, 0.0f);
        if (l >= 0 && static_cast<size_t>(l) < n_classes_) row[l] = 1.0f;
        out.push_back(std::move(row));
    }
    return out;
}

std::vector<int> PreprocessOnehot::InverseTransform(
        const std::vector<std::vector<float>>& encoded) const {
    std::vector<int> out;
    for (const auto& row : encoded)
        out.push_back(static_cast<int>(
            std::max_element(row.begin(), row.end()) - row.begin()));
    return out;
}

}  // namespace XER::ML::Preprocessing
