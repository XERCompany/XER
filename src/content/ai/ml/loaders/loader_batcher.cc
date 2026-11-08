#include "loader_batcher.h"

#include <algorithm>
#include <numeric>
#include <random>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view/chunk.hpp>
#include <range/v3/view/iota.hpp>
#include <range/v3/view/transform.hpp>
#include <range/v3/range/conversion.hpp>

namespace XER::ML::Loaders {

std::vector<Batch> LoaderBatcher::MakeBatches(const Dataset& ds) {
    const size_t n = ds.X.size();

    // Build index order, optionally shuffled
    std::vector<size_t> indices(n);
    std::iota(indices.begin(), indices.end(), 0);
    if (shuffle_) {
        std::mt19937 rng(seed_);
        std::shuffle(indices.begin(), indices.end(), rng);
    }

    // Chunk indices into groups of batch_size_ using range-v3
    auto chunks = indices
        | ranges::views::chunk(static_cast<std::ptrdiff_t>(batch_size_));

    std::vector<Batch> batches;
    for (auto chunk : chunks) {
        Batch b;
        for (size_t i : chunk) {
            b.X.push_back(ds.X[i]);
            if (!ds.y.empty()) b.y.push_back(ds.y[i]);
        }
        batches.push_back(std::move(b));
    }
    return batches;
}

}  // namespace XER::ML::Loaders
