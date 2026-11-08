#include "preprocess_label_encoder.h"
#include <stdexcept>

namespace XER::ML::Preprocessing {

void PreprocessLabelEncoder::Fit(const std::vector<std::string>& labels) {
    label_to_idx_.clear();
    idx_to_label_.clear();
    for (const auto& l : labels)
        if (!label_to_idx_.count(l)) {
            label_to_idx_[l] = static_cast<int>(idx_to_label_.size());
            idx_to_label_.push_back(l);
        }
    fitted_ = true;
}

std::vector<int> PreprocessLabelEncoder::Transform(
        const std::vector<std::string>& labels) const {
    std::vector<int> out;
    out.reserve(labels.size());
    for (const auto& l : labels) {
        auto it = label_to_idx_.find(l);
        out.push_back(it != label_to_idx_.end() ? it->second : -1);
    }
    return out;
}

std::vector<std::string> PreprocessLabelEncoder::InverseTransform(
        const std::vector<int>& encoded) const {
    std::vector<std::string> out;
    out.reserve(encoded.size());
    for (int idx : encoded)
        out.push_back((idx >= 0 && static_cast<size_t>(idx) < idx_to_label_.size())
                      ? idx_to_label_[idx] : "");
    return out;
}

}  // namespace XER::ML::Preprocessing
