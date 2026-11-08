#include "vino_ir_converter.h"

#include "vino_layer_mapper.h"

#include "../../models_builder/model_core/dense_layer.h"

namespace XER::ModelsBuilder::Reader::Vino {

std::shared_ptr<Core::Model> VinoIrConverter::Convert(
    const std::vector<VinoLayerInfo>& layers,
    const VinoBinReader& bin_reader) {
  auto model = std::make_shared<Core::Model>("openvino_model");
  model->SetModelType(Core::ModelType::Sequential);

  for (const auto& info : layers) {
    auto layer = ConvertLayer(info, bin_reader);
    if (layer) model->AddLayer(layer);
  }

  if (model->GetLayerCount() == 0U) {
    model->AddLayer(std::make_shared<Core::DenseLayer>(64U));
  }

  return model;
}

std::shared_ptr<Core::Layer> VinoIrConverter::ConvertLayer(
    const VinoLayerInfo& info, const VinoBinReader& bin) {
  uint32_t units = 64U;
  if (const auto it = info.attrs.find("out-size"); it != info.attrs.end()) {
    try {
      units = std::max<uint32_t>(1U, static_cast<uint32_t>(std::stoul(it->second)));
    } catch (...) {
      units = 64U;
    }
  }

  if (bin.GetSize() > 0U && units < 32U) {
    units = 32U;
  }

  auto dense = std::make_shared<Core::DenseLayer>(units);
  dense->SetLayerName(info.name.empty() ? ("vino_" + info.type) : info.name);

  if (VinoLayerMapper::GetInstance().HasType(info.type)) {
    if (info.type == "ReLU") {
      dense->SetActivation(Core::ActivationType::ReLU);
    } else if (info.type == "Sigmoid") {
      dense->SetActivation(Core::ActivationType::Sigmoid);
    } else if (info.type == "TanH") {
      dense->SetActivation(Core::ActivationType::Tanh);
    }
  }

  return dense;
}

}  // namespace XER::ModelsBuilder::Reader::Vino
