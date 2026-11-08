#pragma once
#include "loader_base.h"
#include <cstddef>

namespace XER::ML::Loaders {

/// Raw binary float loader: reads flat array of float32 from file.
///
/// File layout v1 (legacy):
///   [n_samples: uint32][n_features: uint32][float32 × (n_samples × n_features)]
///
/// File layout v2 (with labels):
///   [n_samples: uint32][n_features: uint32][n_labels: uint32]
///   [float32 × (n_samples × n_features)][int32 × n_labels]
///
/// If n_labels == n_samples the int32 values are loaded as Dataset::y.
/// A file with only the 2-field header is treated as v1 (no labels).
class LoaderBinary : public LoaderBase {
public:
    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "BinaryLoader"; }
};

}  // namespace XER::ML::Loaders
