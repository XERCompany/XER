#pragma once

#include "vino_bin_reader.h"
#include "../../models_builder/model_core/model.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace XER::ModelsBuilder::Reader::Vino {

/// @brief Parsed OpenVINO IR layer info from XML
struct VinoLayerInfo {
  std::string                               id;
  std::string                               name;
  std::string                               type;        ///< e.g. "Convolution", "ReLU"
  std::unordered_map<std::string, std::string> attrs;
  std::vector<std::string>                  input_ids;
  std::vector<std::string>                  output_ids;
};

/// @brief Converts a parsed OpenVINO IR model into an XER Core::Model
class VinoIrConverter {
 public:
  /// Build XER model from parsed IR layers + weight data
  /// @param layers    IR layers parsed from XML
  /// @param bin_reader Loaded .bin file
  std::shared_ptr<Core::Model> Convert(
      const std::vector<VinoLayerInfo>& layers,
      const VinoBinReader& bin_reader);

 private:
  std::shared_ptr<Core::Layer> ConvertLayer(const VinoLayerInfo& info,
                                             const VinoBinReader& bin);
};

}  // namespace XER::ModelsBuilder::Reader::Vino
