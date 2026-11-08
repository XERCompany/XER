#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief 3D Convolution layer (NCDHW)
class LayerConv3D : public Core::Layer {
 public:
  struct Config {
    int in_channels{1}, out_channels{1};
    int kernel_d{3}, kernel_h{3}, kernel_w{3};
    int stride_d{1}, stride_h{1}, stride_w{1};
    int pad_d{0},    pad_h{0},    pad_w{0};
    bool bias{true};
  };

  explicit LayerConv3D(const std::string& name, Config config);
  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeConv3D(const std::string& name,
                                         LayerConv3D::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
