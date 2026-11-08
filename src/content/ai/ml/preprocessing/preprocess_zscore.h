#pragma once
#include "preprocess_base.h"

namespace XER::ML::Preprocessing {

/// Z-score normalisation: same as Standardizer (alias)
class PreprocessZscore : public PreprocessBase {
public:
    void Fit(const std::vector<std::vector<float>>& X) override;
    std::vector<std::vector<float>> Transform(
        const std::vector<std::vector<float>>& X) const override;
    bool IsFitted() const override { return fitted_; }
    std::string Name() const override { return "ZScore"; }
private:
    std::vector<float> mean_, std_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
