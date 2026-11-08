#pragma once

#include "../core_loaders/reader_base.h"
#include <string>

namespace XER::ModelsBuilder::Reader::Framework {

/// @brief Reads TensorFlow SavedModel directory format (saved_model.pb)
class TfSavedModelParser : public ReaderBase {
 public:
  LoadResult Load(const std::string& filepath) override;
  bool CanLoad(const std::string& filepath) const override;
  std::string GetFormatName() const override { return "TF SavedModel"; }
  std::string GetFileExtension() const override { return ""; }  // directory
};

}  // namespace XER::ModelsBuilder::Reader::Framework
