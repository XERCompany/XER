#pragma once
#include "loader_base.h"
#include <cstddef>

namespace XER::ML::Loaders {

struct Batch {
    std::vector<std::vector<float>> X;
    std::vector<int>                y;
};

/// Split a Dataset into mini-batches
class LoaderBatcher {
public:
    explicit LoaderBatcher(size_t batch_size, bool shuffle = false, unsigned seed = 42)
        : batch_size_(batch_size), shuffle_(shuffle), seed_(seed) {}

    /// Partition dataset into batches
    std::vector<Batch> MakeBatches(const Dataset& ds);

private:
    size_t batch_size_;
    bool   shuffle_;
    unsigned seed_;
};

}  // namespace XER::ML::Loaders
