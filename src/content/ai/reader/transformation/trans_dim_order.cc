#include "trans_dim_order.h"
#include "../../models_builder/layer_factory/layer_linear.h"

namespace XER::ModelsBuilder::Reader::Transform {

std::shared_ptr<Core::Model> TransDimOrder::Apply(
    std::shared_ptr<Core::Model> model,
    Layout from_layout, Layout to_layout) {
  if (from_layout == to_layout) return model;

  // Insert Transpose before first layer
  auto transpose = MakeTransposeLayer(from_layout, to_layout);
  auto new_model = std::make_shared<Core::Model>(model->GetModelName());
  new_model->AddLayer(transpose);
  for (size_t i = 0; i < model->GetLayerCount(); ++i)
    new_model->AddLayer(model->GetLayer(i));
  return new_model;
}

std::shared_ptr<Core::Layer> TransDimOrder::MakeTransposeLayer(
    Layout from, Layout to) {
  std::string name;
  if (from == Layout::NCHW && to == Layout::NHWC)
    name = "Transpose_NCHW_to_NHWC";
  else
    name = "Transpose_NHWC_to_NCHW";
  return LayerFactory::MakeLinear(name, {});
}

}  // namespace XER::ModelsBuilder::Reader::Transform
