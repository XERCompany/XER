#include "loader_binary.h"

#include <cstdint>
#include <fstream>
#include <stdexcept>

#include <absl/strings/str_format.h>
#include <absl/strings/string_view.h>
#include <range/v3/view.hpp>

namespace XER::ML::Loaders {

Dataset LoaderBinary::Load(const std::string& path) {
    Dataset cached;
    if (TryLoadCachedDataset(Name(), path, &cached)) {
        return cached;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("BinaryLoader: cannot open " + path);

    uint32_t n_samples = 0, n_features = 0;
    if (!f.read(reinterpret_cast<char*>(&n_samples),  sizeof(uint32_t)) ||
        !f.read(reinterpret_cast<char*>(&n_features), sizeof(uint32_t))) {
        throw std::runtime_error("BinaryLoader: truncated header in " + path);
    }

    // Optional v2 field: number of int32 labels following the float matrix.
    uint32_t n_labels = 0;
    {
        const std::streampos before = f.tellg();
        uint32_t candidate = 0;
        if (f.read(reinterpret_cast<char*>(&candidate), sizeof(uint32_t))) {
            // Peek at expected byte position after the float matrix.
            // If the remaining stream size fits v2 layout, accept n_labels.
            f.seekg(0, std::ios::end);
            const std::streamoff file_size = f.tellg();
            const std::streamoff v2_end = static_cast<std::streamoff>(before)
                + sizeof(uint32_t)
                + static_cast<std::streamoff>(n_samples) * n_features * sizeof(float)
                + static_cast<std::streamoff>(candidate) * sizeof(int32_t);
            if (candidate <= n_samples && v2_end == file_size) {
                n_labels = candidate;
                f.seekg(before + static_cast<std::streamoff>(sizeof(uint32_t)));
            } else {
                f.seekg(before);  // v1: rewind past the peeked bytes
            }
        } else {
            f.clear();
            f.seekg(before);
        }
    }

    Dataset ds;
    ds.X.resize(n_samples, std::vector<float>(n_features));
    for (uint32_t i = 0; i < n_samples; ++i)
        f.read(reinterpret_cast<char*>(ds.X[i].data()),
               n_features * sizeof(float));

    if (n_labels > 0) {
        ds.y.resize(n_labels);
        for (uint32_t i = 0; i < n_labels; ++i) {
            int32_t label = 0;
            f.read(reinterpret_cast<char*>(&label), sizeof(int32_t));
            ds.y[i] = static_cast<int>(label);
        }
    }

    StoreCachedDataset(Name(), path, ds);
    return ds;
}

}  // namespace XER::ML::Loaders
