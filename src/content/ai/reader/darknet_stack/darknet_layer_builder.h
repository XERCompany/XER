#pragma once

#include "darknet_cfg_reader.h"
#include "../../models_builder/model_core/layer.h"
#include <memory>
#include <vector>

namespace XER::ModelsBuilder::Reader::Darknet {

/// @brief Converts Darknet CfgSection into XER Core::Layer objects
class DarknetLayerBuilder {
 public:
  /// Build layers from a parsed config (excluding the [net] section)
  /// @param sections All cfg sections from DarknetCfgReader
  /// @return Vector of XER layers in forward-pass order
  std::vector<std::shared_ptr<Core::Layer>> BuildLayers(
      const std::vector<CfgSection>& sections);

 private:
  std::shared_ptr<Core::Layer> BuildConvolutional(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildConnected(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildMaxpool(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildAvgpool(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildRoute(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildShortcut(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildUpsample(const CfgSection& sec);
  std::shared_ptr<Core::Layer> BuildYolo(const CfgSection& sec);

  static int GetInt(const CfgSection& sec, const std::string& key, int def = 0);
  static float GetFloat(const CfgSection& sec, const std::string& key, float def = 0.0f);
  static std::string GetStr(const CfgSection& sec, const std::string& key,
                             const std::string& def = "");
};

}  // namespace XER::ModelsBuilder::Reader::Darknet
