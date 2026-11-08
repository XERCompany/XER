#include "trans_fuse_identity.h"

namespace XER::ModelsBuilder::Reader::Transform {

std::shared_ptr<Core::Model> TransFuseIdentity::Apply(
    std::shared_ptr<Core::Model> model) {
  auto new_model = std::make_shared<Core::Model>(model->GetModelName());
  for (size_t i = 0; i < model->GetLayerCount(); ++i) {
    auto layer = model->GetLayer(i);
    if (layer && !IsIdentityLayer(*layer))
      new_model->AddLayer(layer);
  }
  return new_model;
}

bool TransFuseIdentity::IsIdentityLayer(const Core::Layer& layer) {
  const auto& name = layer.GetLayerName();
  return name == "Identity" || name == "Pass" || name == "NoOp";
}

}  // namespace XER::ModelsBuilder::Reader::Transform
