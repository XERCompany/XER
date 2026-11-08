#pragma once

#include "../../models_builder/model_core/model.h"
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::Reader::Schema {

/// @brief Extracts metadata fields from a loaded model
struct ModelMetadata {
  std::string name;
  std::string format;        ///< "onnx", "darknet", "openvino"
  std::string description;
  std::string version;
  std::unordered_map<std::string, std::string> custom_fields;
  size_t      layer_count{0};
  size_t      param_count{0};   ///< Estimated parameter count
};

class MetadataExtractor {
 public:
  /// Extract metadata from a Core::Model
  static ModelMetadata Extract(const Core::Model& model,
                                const std::string& format = "");

  /// Serialize metadata to JSON-like string
  static std::string ToJsonString(const ModelMetadata& meta);
};

}  // namespace XER::ModelsBuilder::Reader::Schema
