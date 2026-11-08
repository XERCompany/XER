#pragma once

#include "../core_loaders/reader_base.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::Reader::Framework {

/// @brief Reads Caffe .prototxt + .caffemodel pairs
class CaffeProtoxtReader : public ReaderBase {
 public:
  LoadResult Load(const std::string& filepath) override;
  bool CanLoad(const std::string& filepath) const override;
  std::string GetFormatName() const override { return "Caffe"; }
  std::string GetFileExtension() const override { return ".prototxt"; }

  /// Load weights separately from .caffemodel
  bool LoadWeights(const std::string& caffemodel_path);

 private:
  std::string weights_path_;
};

}  // namespace XER::ModelsBuilder::Reader::Framework
