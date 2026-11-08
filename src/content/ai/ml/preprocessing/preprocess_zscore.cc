#include "preprocess_zscore.h"
#include "preprocess_standardizer.h"

namespace XER::ML::Preprocessing {

void PreprocessZscore::Fit(const std::vector<std::vector<float>>& X) {
    PreprocessStandardizer s;
    s.Fit(X);
    mean_ = s.Means();
    std_  = s.StdDevs();
    fitted_ = true;
}

std::vector<std::vector<float>> PreprocessZscore::Transform(
        const std::vector<std::vector<float>>& X) const {
    auto out = X;
    for (auto& row : out)
        for (size_t j = 0; j < row.size() && j < mean_.size(); ++j)
            row[j] = (row[j] - mean_[j]) / std_[j];
    return out;
}

}  // namespace XER::ML::Preprocessing
