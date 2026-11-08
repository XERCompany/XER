#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Multi-Head Self-Attention layer (Transformer-style)
class LayerAttention : public Core::Layer {
 public:
  struct Config {
    int  embed_dim{512};
    int  num_heads{8};
    bool causal_mask{false};
    float dropout{0.0f};
  };

  explicit LayerAttention(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeAttention(const std::string& name,
                                              LayerAttention::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
