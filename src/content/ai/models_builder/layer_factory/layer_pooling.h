#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief Pooling layer (MaxPool2D, AvgPool2D, GlobalAvgPool2D)
class LayerPooling : public Core::Layer {
 public:
  enum class PoolType { MaxPool, AvgPool, GlobalAvgPool };

  struct Config {
    PoolType pool_type{PoolType::MaxPool};
    int kernel_h{2}, kernel_w{2};
    int stride_h{2}, stride_w{2};
    int pad_h{0},    pad_w{0};
  };

  explicit LayerPooling(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakePooling(const std::string& name,
                                          LayerPooling::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
