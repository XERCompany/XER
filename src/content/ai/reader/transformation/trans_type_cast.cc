#include "trans_type_cast.h"
#include "../../models_builder/layer_factory/layer_linear.h"

namespace XER::ModelsBuilder::Reader::Transform {

std::shared_ptr<Core::Model> TransTypeCast::Apply(
    std::shared_ptr<Core::Model> model, DType target_dtype) {
  // For float32 target: no casts needed (XER native dtype)
  if (target_dtype == DType::Float32) return model;

  // For fp16/int8: wrap model in a new model with Cast before and after
  std::string cast_name =
      (target_dtype == DType::Float16) ? "Cast_Float16" : "Cast_Int8";

  auto new_model = std::make_shared<Core::Model>(model->GetModelName());
  new_model->AddLayer(LayerFactory::MakeLinear(cast_name + "_in", {}));
  for (size_t i = 0; i < model->GetLayerCount(); ++i)
    new_model->AddLayer(model->GetLayer(i));
  new_model->AddLayer(LayerFactory::MakeLinear("Cast_Float32_out", {}));
  return new_model;
}

}  // namespace XER::ModelsBuilder::Reader::Transform
