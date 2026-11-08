#include "preprocess_resampler.h"

namespace XER::ML::Preprocessing {

std::vector<std::vector<float>> PreprocessResampler::Transform(
        const std::vector<std::vector<float>>& X) const {
    std::vector<std::vector<float>> out;
    out.reserve(X.size());
    for (const auto& row : X) {
        const size_t src_len = row.size();
        if (src_len == 0 || target_length_ == 0) { out.push_back({}); continue; }
        std::vector<float> resampled(target_length_);
        for (size_t i = 0; i < target_length_; ++i) {
            float src_idx = static_cast<float>(i) * (src_len - 1) / (target_length_ - 1 > 0 ? target_length_ - 1 : 1);
            size_t lo = static_cast<size_t>(src_idx);
            size_t hi = lo + 1 < src_len ? lo + 1 : lo;
            float t = src_idx - static_cast<float>(lo);
            resampled[i] = row[lo] * (1.0f - t) + row[hi] * t;
        }
        out.push_back(std::move(resampled));
    }
    return out;
}

}  // namespace XER::ML::Preprocessing
