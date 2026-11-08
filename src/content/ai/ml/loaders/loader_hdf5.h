#pragma once
#include "loader_base.h"

namespace XER::ML::Loaders {

/// HDF5 dataset loader (requires libhdf5, link with -lhdf5_cpp -lhdf5).
/// Expects datasets named "X" (float32, N×F) and optionally "y" (int32, N).
class LoaderHdf5 : public LoaderBase {
public:
    Dataset Load(const std::string& path) override;
    std::string Name() const override { return "Hdf5Loader"; }
};

}  // namespace XER::ML::Loaders
