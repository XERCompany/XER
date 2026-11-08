#pragma once

#include "../core_loaders/reader_base.h"
#include <string>

namespace XER::ModelsBuilder::Reader::Framework {

/// @brief Reads TensorFlow SavedModel protobuf (.pb) format
///
/// Parses GraphDef binary protobuf into XER Core::Model.
/// Full support requires protobuf + tensorflow protos.
class TfPbReader : public ReaderBase {
 public:
  LoadResult Load(const std::string& filepath) override;
  bool CanLoad(const std::string& filepath) const override;
  std::string GetFormatName() const override { return "TensorFlow PB"; }
  std::string GetFileExtension() const override { return ".pb"; }
};

}  // namespace XER::ModelsBuilder::Reader::Framework
