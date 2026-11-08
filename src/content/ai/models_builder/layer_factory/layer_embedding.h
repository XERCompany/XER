#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Embedding lookup layer (vocabulary → dense vectors)
class LayerEmbedding : public Core::Layer {
 public:
  struct Config {
    int vocab_size{1};
    int embed_dim{64};
    int padding_idx{-1};
  };

  explicit LayerEmbedding(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeEmbedding(const std::string& name,
                                              LayerEmbedding::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
