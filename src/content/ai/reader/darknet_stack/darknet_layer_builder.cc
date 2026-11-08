#include "darknet_layer_builder.h"

#include "../../models_builder/layer_factory/layer_conv2d.h"
#include "../../models_builder/layer_factory/layer_linear.h"
#include "../../models_builder/layer_factory/layer_pooling.h"

#include <cstdlib>
#include <sstream>

namespace XER::ModelsBuilder::Reader::Darknet {

int DarknetLayerBuilder::GetInt(const CfgSection& sec,
                                 const std::string& key, int def) {
  auto it = sec.params.find(key);
  if (it == sec.params.end()) return def;
  return std::stoi(it->second);
}

float DarknetLayerBuilder::GetFloat(const CfgSection& sec,
                                     const std::string& key, float def) {
  auto it = sec.params.find(key);
  if (it == sec.params.end()) return def;
  return std::stof(it->second);
}

std::string DarknetLayerBuilder::GetStr(const CfgSection& sec,
                                         const std::string& key,
                                         const std::string& def) {
  auto it = sec.params.find(key);
  if (it == sec.params.end()) return def;
  return it->second;
}

std::vector<std::shared_ptr<Core::Layer>> DarknetLayerBuilder::BuildLayers(
    const std::vector<CfgSection>& sections) {
  std::vector<std::shared_ptr<Core::Layer>> layers;

  for (auto& sec : sections) {
    if (sec.name == "net") continue;  // Global config, not a layer

    std::shared_ptr<Core::Layer> layer;
    if      (sec.name == "convolutional")  layer = BuildConvolutional(sec);
    else if (sec.name == "connected")      layer = BuildConnected(sec);
    else if (sec.name == "maxpool")        layer = BuildMaxpool(sec);
    else if (sec.name == "avgpool")        layer = BuildAvgpool(sec);
    else if (sec.name == "route")          layer = BuildRoute(sec);
    else if (sec.name == "shortcut")       layer = BuildShortcut(sec);
    else if (sec.name == "upsample")       layer = BuildUpsample(sec);
    else if (sec.name == "yolo")           layer = BuildYolo(sec);
    else {
      // Unknown section — wrap as a generic Dense layer with section name
      LayerFactory::LayerLinear::Config cfg;
      layer = LayerFactory::MakeLinear(sec.name, cfg);
    }

    if (layer) layers.push_back(std::move(layer));
  }
  return layers;
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildConvolutional(
    const CfgSection& sec) {
  LayerFactory::LayerConv2D::Config cfg;
  cfg.out_channels = GetInt(sec, "filters", 32);
  int ks           = GetInt(sec, "size", 3);
  cfg.kernel_h = cfg.kernel_w = ks;
  int st           = GetInt(sec, "stride", 1);
  cfg.stride_h = cfg.stride_w = st;
  int pad          = GetInt(sec, "pad", 0);
  cfg.pad_h = cfg.pad_w = pad;
  (void)GetStr(sec, "activation", "leaky");
  (void)GetInt(sec, "batch_normalize", 0);
  return LayerFactory::MakeConv2D("Conv", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildConnected(
    const CfgSection& sec) {
  LayerFactory::LayerLinear::Config cfg;
  cfg.out_features = GetInt(sec, "output", 1024);
  (void)GetStr(sec, "activation", "linear");
  return LayerFactory::MakeLinear("Dense", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildMaxpool(
    const CfgSection& sec) {
  LayerFactory::LayerPooling::Config cfg;
  cfg.pool_type = LayerFactory::LayerPooling::PoolType::MaxPool;
  int ks = GetInt(sec, "size", 2);
  cfg.kernel_h = cfg.kernel_w = ks;
  int st = GetInt(sec, "stride", 2);
  cfg.stride_h = cfg.stride_w = st;
  return LayerFactory::MakePooling("MaxPool", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildAvgpool(
    const CfgSection& /*sec*/) {
  LayerFactory::LayerPooling::Config cfg;
  cfg.pool_type = LayerFactory::LayerPooling::PoolType::GlobalAvgPool;
  return LayerFactory::MakePooling("GlobalAvgPool", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildRoute(
    const CfgSection& sec) {
  (void)GetStr(sec, "layers", "");
  LayerFactory::LayerLinear::Config cfg;
  return LayerFactory::MakeLinear("Route", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildShortcut(
    const CfgSection& sec) {
  (void)GetInt(sec, "from", -3);
  LayerFactory::LayerLinear::Config cfg;
  return LayerFactory::MakeLinear("Shortcut", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildUpsample(
    const CfgSection& sec) {
  (void)GetInt(sec, "stride", 2);
  LayerFactory::LayerLinear::Config cfg;
  return LayerFactory::MakeLinear("Upsample", cfg);
}

std::shared_ptr<Core::Layer> DarknetLayerBuilder::BuildYolo(
    const CfgSection& sec) {
  (void)GetStr(sec, "anchors", "");
  (void)GetInt(sec, "classes", 80);
  LayerFactory::LayerLinear::Config cfg;
  return LayerFactory::MakeLinear("YOLO", cfg);
}

}  // namespace XER::ModelsBuilder::Reader::Darknet
