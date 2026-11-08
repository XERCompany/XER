#pragma once
#include "preprocess_base.h"
#include <map>
#include <string>
#include <vector>

namespace XER::ML::Preprocessing {

/// Encode string or integer class labels to integer indices [0, n_classes)
class PreprocessLabelEncoder {
public:
    /// Fit on string labels
    void Fit(const std::vector<std::string>& labels);
    /// Encode labels to ints
    std::vector<int> Transform(const std::vector<std::string>& labels) const;
    /// Decode ints back to strings
    std::vector<std::string> InverseTransform(const std::vector<int>& encoded) const;

    bool IsFitted() const { return fitted_; }
    size_t NumClasses() const { return idx_to_label_.size(); }

private:
    std::map<std::string, int> label_to_idx_;
    std::vector<std::string>   idx_to_label_;
    bool fitted_ = false;
};

}  // namespace XER::ML::Preprocessing
