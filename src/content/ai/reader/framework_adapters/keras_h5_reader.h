#pragma once

#include "../core_loaders/reader_base.h"
#include <string>

namespace XER::ModelsBuilder::Reader::Framework {

/// @brief Reads Keras HDF5 (.h5) model files
///
/// Parses HDF5 group structure: model_config JSON + weight datasets.
/// Requires libhdf5; until linked, stubs return placeholder models.
class KerasH5Reader : public ReaderBase {
 public:
  LoadResult Load(const std::string& filepath) override;
  bool CanLoad(const std::string& filepath) const override;
  std::string GetFormatName() const override { return "Keras HDF5"; }
  std::string GetFileExtension() const override { return ".h5"; }
};

}  // namespace XER::ModelsBuilder::Reader::Framework
