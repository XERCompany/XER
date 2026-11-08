#pragma once

#include "vino_ir_converter.h"
#include "../../models_builder/model_core/model.h"
#include <memory>
#include <string>
#include <vector>

namespace XER::ModelsBuilder::Reader::Vino {

/// @brief Reads OpenVINO IR XML (v10/v11) and builds VinoLayerInfo list
class VinoXmlReader {
 public:
  /// Parse an OpenVINO .xml model file
  /// @param xml_path Path to .xml file
  bool ParseFile(const std::string& xml_path);

  /// Parse from in-memory XML string
  bool ParseString(const std::string& xml_content);

  /// Get parsed layers
  const std::vector<VinoLayerInfo>& GetLayers() const { return layers_; }

  /// Get IR version parsed from XML
  int GetIrVersion() const { return ir_version_; }

  /// Get model name from XML
  const std::string& GetModelName() const { return model_name_; }

  /// Build full model using the paired .bin file
  std::shared_ptr<Core::Model> BuildModel(const VinoBinReader& bin_reader);

 private:
  void ParseLayerElement(const std::string& xml_layer_block);

  std::vector<VinoLayerInfo> layers_;
  int                        ir_version_{11};
  std::string                model_name_{"openvino_model"};
};

}  // namespace XER::ModelsBuilder::Reader::Vino
