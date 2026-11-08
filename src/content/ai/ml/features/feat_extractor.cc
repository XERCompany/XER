#include "feat_extractor.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace XER::ML::Features {

FeatureResult FeatExtractor::ExtractStats(const std::vector<float>& signal,
                                           size_t window_size) const {
    FeatureResult r;
    r.name = "stats";
    if (signal.empty()) return r;
    if (window_size == 0 || window_size >= signal.size()) {
        // Whole-signal stats
        float mean = FeatMathOps::Mean(signal);
        float var  = FeatMathOps::Variance(signal);
        float mn   = *std::min_element(signal.begin(), signal.end());
        float mx   = *std::max_element(signal.begin(), signal.end());
        float l2   = FeatMathOps::L2Norm(signal);
        r.values   = {mean, var, mn, mx, l2};
    } else {
        for (size_t off = 0; off + window_size <= signal.size(); off += window_size) {
            std::vector<float> w(signal.begin() + off, signal.begin() + off + window_size);
            float mean = FeatMathOps::Mean(w);
            float var  = FeatMathOps::Variance(w);
            float mn   = *std::min_element(w.begin(), w.end());
            float mx   = *std::max_element(w.begin(), w.end());
            r.values.insert(r.values.end(), {mean, var, mn, mx});
        }
    }
    r.n_features = r.values.size();
    return r;
}

FeatureResult FeatExtractor::ExtractHistogram(const std::vector<float>& signal, int bins) const {
    FeatureResult r;
    r.name = "histogram";
    if (signal.empty() || bins <= 0) return r;
    float mn = *std::min_element(signal.begin(), signal.end());
    float mx = *std::max_element(signal.begin(), signal.end());
    float range = mx - mn;
    if (range < 1e-9f) range = 1.0f;
    r.values.resize(bins, 0.0f);
    for (auto v : signal) {
        int b = static_cast<int>((v - mn) / range * bins);
        b = std::clamp(b, 0, bins - 1);
        r.values[b] += 1.0f;
    }
    // Normalize
    float sum = static_cast<float>(signal.size());
    for (auto& v : r.values) v /= sum;
    r.n_features = bins;
    return r;
}

FeatureResult FeatExtractor::ExtractDelta(const std::vector<float>& signal) const {
    FeatureResult r;
    r.name = "delta";
    if (signal.size() < 2) return r;
    r.values.resize(signal.size() - 1);
    for (size_t i = 0; i < r.values.size(); ++i)
        r.values[i] = signal[i + 1] - signal[i];
    r.n_features = r.values.size();
    return r;
}

std::vector<float> FeatExtractor::Flatten(const std::vector<FeatureResult>& results) {
    std::vector<float> out;
    for (const auto& r : results)
        out.insert(out.end(), r.values.begin(), r.values.end());
    return out;
}

}  // namespace XER::ML::Features
