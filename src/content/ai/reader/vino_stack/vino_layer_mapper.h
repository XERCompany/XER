#pragma once

#include "vino_bin_reader.h"
#include "vino_ir_converter.h"
#include "../../models_builder/model_core/model.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::Reader::Vino {

/// @brief Maps OpenVINO layer type strings → XER layer factories
///
/// Provides operator → layer creation mapping similar to OnnxNodeMap.
class VinoLayerMapper {
 public:
  static VinoLayerMapper& GetInstance();

  /// Check if OpenVINO layer type is supported
  bool HasType(const std::string& type) const;

  /// List all supported OpenVINO layer types
  std::vector<std::string> GetSupportedTypes() const;

 private:
  VinoLayerMapper();
  std::unordered_map<std::string, std::string> type_map_; ///< OV→XER name
};

}  // namespace XER::ModelsBuilder::Reader::Vino
