#include "val_accuracy.h"
#include <algorithm>
#include <numeric>

namespace XER::ML::Validation {

float ValAccuracy::Compute(const std::vector<int>& predictions,
                            const std::vector<int>& ground_truth) {
    if (predictions.size() != ground_truth.size() || predictions.empty()) return 0.0f;
    size_t correct = 0;
    for (size_t i = 0; i < predictions.size(); ++i)
        if (predictions[i] == ground_truth[i]) ++correct;
    return static_cast<float>(correct) / static_cast<float>(predictions.size());
}

float ValAccuracy::TopK(const std::vector<float>& probs, size_t n_classes,
                         const std::vector<int>& ground_truth, int k) {
    const size_t n = ground_truth.size();
    if (n == 0 || probs.size() != n * n_classes) return 0.0f;
    size_t correct = 0;
    for (size_t i = 0; i < n; ++i) {
        const float* row = probs.data() + i * n_classes;
        // Find top-k class indices
        std::vector<int> idx(n_classes);
        std::iota(idx.begin(), idx.end(), 0);
        std::partial_sort(idx.begin(), idx.begin() + std::min(static_cast<size_t>(k), n_classes),
                          idx.end(), [&](int a, int b){ return row[a] > row[b]; });
        for (int j = 0; j < k && j < static_cast<int>(n_classes); ++j)
            if (idx[j] == ground_truth[i]) { ++correct; break; }
    }
    return static_cast<float>(correct) / static_cast<float>(n);
}

}  // namespace XER::ML::Validation
