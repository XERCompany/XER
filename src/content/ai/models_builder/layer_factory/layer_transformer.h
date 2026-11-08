#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Full Transformer encoder block (Self-Attn + FFN + LayerNorm)
class LayerTransformer : public Core::Layer {
 public:
  struct Config {
    int   embed_dim{512};
    int   num_heads{8};
    int   ffn_dim{2048};
    float dropout{0.1f};
  };

  explicit LayerTransformer(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeTransformer(const std::string& name,
                                               LayerTransformer::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
