#include "vino_layer_mapper.h"

namespace XER::ModelsBuilder::Reader::Vino {

VinoLayerMapper& VinoLayerMapper::GetInstance() {
  static VinoLayerMapper instance;
  return instance;
}

VinoLayerMapper::VinoLayerMapper() {
  type_map_ = {
    {"Convolution", "Conv"},       {"Deconvolution", "ConvTranspose"},
    {"FullyConnected", "Dense"},   {"ReLU", "Relu"},
    {"Sigmoid", "Sigmoid"},        {"TanH", "Tanh"},
    {"Clamp", "Clamp"},            {"SoftMax", "Softmax"},
    {"BatchNormalization", "BatchNormalization"},
    {"MaxPool", "MaxPool"},        {"AvgPool", "AveragePool"},
    {"Concat", "Concat"},          {"Eltwise", "Add"},
    {"Reshape", "Reshape"},        {"Flatten", "Flatten"},
    {"Transpose", "Transpose"},    {"LSTM", "LSTM"},
    {"GRU", "GRU"},                {"Embedding", "Embedding"},
    {"Dropout", "Dropout"},        {"Pad", "Pad"},
    {"Interpolate", "Upsample"},   {"ShapeOf", "ShapeOf"},
  };
}

bool VinoLayerMapper::HasType(const std::string& type) const {
  return type_map_.count(type) > 0;
}

std::vector<std::string> VinoLayerMapper::GetSupportedTypes() const {
  std::vector<std::string> out;
  out.reserve(type_map_.size());
  for (auto& [k, _] : type_map_) out.push_back(k);
  return out;
}

}  // namespace XER::ModelsBuilder::Reader::Vino
