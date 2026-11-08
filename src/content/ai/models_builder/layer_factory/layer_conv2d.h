#pragma once

#include "../model_core/layer.h"
#include <memory>
#include <string>

namespace XER::ModelsBuilder::LayerFactory {

/// @brief 2D Convolution layer (NCHW, CUDNN-backed)
class LayerConv2D : public Core::Layer {
 public:
  struct Config {
    int in_channels{1};
    int out_channels{1};
    int kernel_h{3}, kernel_w{3};
    int stride_h{1}, stride_w{1};
    int pad_h{0},    pad_w{0};
    int dilation{1};
    int groups{1};
    bool bias{true};
  };

  explicit LayerConv2D(const std::string& name, Config config);

  const Config& GetConfig() const { return config_; }

 private:
  Config config_;
};

std::shared_ptr<Core::Layer> MakeConv2D(const std::string& name,
                                         LayerConv2D::Config config);

}  // namespace XER::ModelsBuilder::LayerFactory
