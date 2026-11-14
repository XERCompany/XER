#include "schema_validator.h"

#include "../../models_builder/model_core/dense_layer.h"

#include <memory>

namespace XER::ModelsBuilder::Reader::Schema {

SchemaValidator::ValidationResult SchemaValidator::Validate(
    const Core::Model& model) const {
  ValidationResult result;
  CheckNonEmpty(model, result.errors);
  CheckLayerNames(model, result.errors);
  CheckWeightsAndShapes(model, result);
  result.valid = result.errors.empty();
  return result;
}

void SchemaValidator::CheckWeightsAndShapes(const Core::Model& model,
                                            ValidationResult& result) const {
  for (size_t i = 0; i < model.GetLayerCount(); ++i) {
    const std::shared_ptr<Core::Layer> layer = model.GetLayer(i);
    if (!layer) {
      continue;
    }
    const auto dense = std::dynamic_pointer_cast<Core::DenseLayer>(layer);
    if (!dense) {
      continue;
    }
    const std::string prefix = "Layer '" + layer->GetLayerName() + "': ";
    if (!dense->HasWeights()) {
      result.warnings.push_back(
          prefix + "no weights attached — inference uses synthetic fallback");
      continue;
    }
    const size_t expected = static_cast<size_t>(dense->GetUnits()) *
                            dense->GetInputUnits();
    if (dense->GetInputUnits() > 0U &&
        dense->GetWeights().size() != expected) {
      result.errors.push_back(
          prefix + "weight count " + std::to_string(dense->GetWeights().size()) +
          " != units*input_units " + std::to_string(expected));
    }
    if (dense->GetBias().empty()) {
      result.warnings.push_back(prefix + "weights present but bias is empty");
    }
  }

  if (model.GetOutputShape().empty()) {
    result.warnings.push_back("Model output shape is empty");
  }
}

bool SchemaValidator::CheckLayerNames(const Core::Model& model,
                                       std::vector<std::string>& errors) const {
  bool ok = true;
  for (size_t i = 0; i < model.GetLayerCount(); ++i) {
    auto layer = model.GetLayer(i);
    if (!layer || layer->GetLayerName().empty()) {
      errors.push_back("Layer " + std::to_string(i) + " has empty name");
      ok = false;
    }
  }
  return ok;
}

bool SchemaValidator::CheckNonEmpty(const Core::Model& model,
                                     std::vector<std::string>& errors) const {
  if (model.GetLayerCount() == 0) {
    errors.push_back("Model '" + model.GetModelName() + "' has no layers");
    return false;
  }
  return true;
}

}  // namespace XER::ModelsBuilder::Reader::Schema
