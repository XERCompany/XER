#include "schema_validator.h"

namespace XER::ModelsBuilder::Reader::Schema {

SchemaValidator::ValidationResult SchemaValidator::Validate(
    const Core::Model& model) const {
  ValidationResult result;
  CheckNonEmpty(model, result.errors);
  CheckLayerNames(model, result.errors);
  result.valid = result.errors.empty();
  return result;
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
