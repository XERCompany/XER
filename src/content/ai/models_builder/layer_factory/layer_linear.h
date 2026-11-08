#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Fully-connected (linear/dense) layer
class LayerLinear : public Core::Layer {
 public:
  struct Config {
    int in_features{1};
    int out_features{1};
    bool bias{true};
  };

  explicit LayerLinear(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeLinear(const std::string& name,
                                          LayerLinear::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
